#include "Database.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QVariant>

#include <cstring>
#include <optional>
#include <spdlog/spdlog.h>

// 表结构见 docs/DatabaseDesign.md。
// frame_data / axis_samples 用 BLOB + run_len，省的是「每帧每键一行」的库体积。

namespace {

constexpr auto kConnection = "pc";
constexpr int kSchemaVersion = 1;

const char* kCreateStatements[] = {
    R"SQL(CREATE TABLE IF NOT EXISTS schema_version (
        version INTEGER NOT NULL
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS key_codes (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        key_id TEXT NOT NULL UNIQUE,
        kind TEXT NOT NULL,
        value_kind TEXT NOT NULL,
        range_min REAL,
        range_max REAL,
        native_usage_page INTEGER,
        native_usage INTEGER,
        native_vk INTEGER,
        default_label TEXT NOT NULL,
        origin TEXT NOT NULL DEFAULT 'user',
        notes TEXT,
        created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
        updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS sessions (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        display_name TEXT NOT NULL,
        start_time INTEGER NOT NULL,
        end_time INTEGER,
        fps INTEGER NOT NULL DEFAULT 60,
        total_frames INTEGER NOT NULL DEFAULT 0,
        total_events INTEGER NOT NULL DEFAULT 0,
        format_version INTEGER NOT NULL DEFAULT 1,
        event_log_path TEXT NOT NULL,
        recording_config_snapshot TEXT NOT NULL,
        profile_name_snapshot TEXT,
        note TEXT,
        created_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now')),
        updated_at INTEGER NOT NULL DEFAULT (strftime('%s', 'now'))
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS session_keys (
        session_id INTEGER NOT NULL,
        key_index INTEGER NOT NULL,
        key_code_id INTEGER NOT NULL,
        PRIMARY KEY (session_id, key_index),
        FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
        FOREIGN KEY (key_code_id) REFERENCES key_codes(id)
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS session_axes (
        session_id INTEGER NOT NULL,
        axis_index INTEGER NOT NULL,
        key_code_id INTEGER NOT NULL,
        PRIMARY KEY (session_id, axis_index),
        FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
        FOREIGN KEY (key_code_id) REFERENCES key_codes(id)
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS frame_data (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        session_id INTEGER NOT NULL,
        start_frame INTEGER NOT NULL,
        run_len INTEGER NOT NULL,
        state_blob BLOB NOT NULL,
        FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
        UNIQUE(session_id, start_frame)
    ))SQL", // 数字通道：bitset BLOB，run_len 为连续相同状态的帧数
    R"SQL(CREATE TABLE IF NOT EXISTS axis_samples (
        session_id INTEGER NOT NULL,
        start_frame INTEGER NOT NULL,
        run_len INTEGER NOT NULL,
        values_blob BLOB NOT NULL,
        FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
        UNIQUE(session_id, start_frame)
    ))SQL", // 线性通道：float32 向量 BLOB，RLE 对齐 frame_data
    R"SQL(CREATE TABLE IF NOT EXISTS markers (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        session_id INTEGER NOT NULL,
        frame_index INTEGER NOT NULL,
        name TEXT NOT NULL,
        note TEXT NOT NULL DEFAULT '',
        FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
        UNIQUE(session_id, frame_index, name)
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS tags (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        tag TEXT NOT NULL UNIQUE
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS session_tags (
        session_id INTEGER NOT NULL,
        tag_id INTEGER NOT NULL,
        PRIMARY KEY (session_id, tag_id),
        FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE,
        FOREIGN KEY (tag_id) REFERENCES tags(id) ON DELETE CASCADE
    ))SQL",
    "CREATE INDEX IF NOT EXISTS idx_sessions_start ON sessions(start_time DESC)",
    "CREATE INDEX IF NOT EXISTS idx_markers_session ON markers(session_id, frame_index)",
    "CREATE INDEX IF NOT EXISTS idx_key_codes_native_vk ON key_codes(kind, native_vk)",
    "CREATE INDEX IF NOT EXISTS idx_key_codes_hid ON key_codes(native_usage_page, native_usage)",
};

struct BuiltinKey {
    const char* keyId;
    const char* kind;
    const char* valueKind;
    const char* label;
    std::optional<double> min;
    std::optional<double> max;
    std::optional<int> nativeVk;
};

const BuiltinKey kBuiltins[] = {
    {"w", "keyboard", "digital", "W", std::nullopt, std::nullopt, 0x57},
    {"a", "keyboard", "digital", "A", std::nullopt, std::nullopt, 0x41},
    {"s", "keyboard", "digital", "S", std::nullopt, std::nullopt, 0x53},
    {"d", "keyboard", "digital", "D", std::nullopt, std::nullopt, 0x44},
    {"space", "keyboard", "digital", "Space", std::nullopt, std::nullopt, 0x20},
    {"shift-left", "keyboard", "digital", "Shift", std::nullopt, std::nullopt, 0xA0},
    {"mouse-left", "mouse", "digital", "LMB", std::nullopt, std::nullopt, 0x01},
    {"mouse-right", "mouse", "digital", "RMB", std::nullopt, std::nullopt, 0x02},
    {"mouse-dx", "mouse", "analog", "Mouse DX", -1.0, 1.0, std::nullopt},
    {"mouse-dy", "mouse", "analog", "Mouse DY", -1.0, 1.0, std::nullopt},
    {"pad-a", "gamepad", "digital", "A", std::nullopt, std::nullopt, std::nullopt},
    {"pad-b", "gamepad", "digital", "B", std::nullopt, std::nullopt, std::nullopt},
    {"pad-x", "gamepad", "digital", "X", std::nullopt, std::nullopt, std::nullopt},
    {"pad-y", "gamepad", "digital", "Y", std::nullopt, std::nullopt, std::nullopt},
    {"pad-lt", "gamepad", "analog", "LT", 0.0, 1.0, std::nullopt},
    {"pad-rt", "gamepad", "analog", "RT", 0.0, 1.0, std::nullopt},
    {"pad-lx", "gamepad", "analog", "Left X", -1.0, 1.0, std::nullopt},
    {"pad-ly", "gamepad", "analog", "Left Y", -1.0, 1.0, std::nullopt},
};

QSqlDatabase db() {
    return QSqlDatabase::database(QLatin1String(kConnection));
}

std::optional<int> optionalInt(const QVariant& v) {
    if (v.isNull()) {
        return std::nullopt;
    }
    return v.toInt();
}

std::optional<double> optionalDouble(const QVariant& v) {
    if (v.isNull()) {
        return std::nullopt;
    }
    return v.toDouble();
}

std::optional<KeyCodeRecord> readKeyCode(QSqlQuery& q) {
    if (!q.next()) {
        return std::nullopt;
    }
    KeyCodeRecord row;
    row.id = q.value(0).toLongLong();
    row.keyId = q.value(1).toString();
    row.kind = q.value(2).toString();
    row.valueKind = q.value(3).toString();
    row.rangeMin = optionalDouble(q.value(4));
    row.rangeMax = optionalDouble(q.value(5));
    row.nativeUsagePage = optionalInt(q.value(6));
    row.nativeUsage = optionalInt(q.value(7));
    row.nativeVk = optionalInt(q.value(8));
    row.defaultLabel = q.value(9).toString();
    row.origin = q.value(10).toString();
    return row;
}

const char* kSelectKeyCode =
    "SELECT id, key_id, kind, value_kind, range_min, range_max, "
    "native_usage_page, native_usage, native_vk, default_label, origin "
    "FROM key_codes ";

} // namespace

bool Database::execSql(const QString& sql) {
    QSqlQuery q(db());
    if (!q.exec(sql)) {
        spdlog::error("[db] exec failed: {} sql={}", q.lastError().text().toStdString(),
                      sql.left(80).toStdString());
        return false;
    }
    return true;
}

bool Database::open(const QString& dbFilePath) {
    if (QSqlDatabase::contains(QLatin1String(kConnection))) {
        QSqlDatabase::removeDatabase(QLatin1String(kConnection));
    }
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        spdlog::critical("[db] Qt QSQLITE driver missing");
        return false;
    }

    QDir().mkpath(QFileInfo(dbFilePath).absolutePath());
    auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QLatin1String(kConnection));
    database.setDatabaseName(dbFilePath);
    if (!database.open()) {
        spdlog::critical("[db] open failed path={} err={}", dbFilePath.toStdString(),
                         database.lastError().text().toStdString());
        return false;
    }
    {
        QSqlQuery pragma(database);
        if (!pragma.exec(QStringLiteral("PRAGMA foreign_keys = ON"))) {
            spdlog::warn("[db] PRAGMA foreign_keys failed: {}",
                         pragma.lastError().text().toStdString());
        }
        if (!pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"))) {
            spdlog::warn("[db] PRAGMA journal_mode=WAL failed: {}",
                         pragma.lastError().text().toStdString());
        }
    }
    spdlog::info("[db] opened {}", dbFilePath.toStdString());
    if (!migrate() || !seedBuiltins()) {
        return false;
    }
    return true;
}

void Database::close() {
    if (!QSqlDatabase::contains(QLatin1String(kConnection))) {
        return;
    }
    {
        auto database = db();
        if (database.isOpen()) {
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(QLatin1String(kConnection));
    spdlog::info("[db] closed");
}

bool Database::isOpen() const {
    return QSqlDatabase::contains(QLatin1String(kConnection)) && db().isOpen();
}

std::optional<KeyCodeRecord> Database::findByKeyId(const QString& keyId) const {
    if (!isOpen()) {
        spdlog::error("[db] findByKeyId: database not open");
        return std::nullopt;
    }
    QSqlQuery q(db());
    q.prepare(QString::fromLatin1(kSelectKeyCode) + QStringLiteral("WHERE key_id = ?"));
    q.addBindValue(keyId);
    if (!q.exec()) {
        spdlog::error("[db] findByKeyId failed: {}", q.lastError().text().toStdString());
        return std::nullopt;
    }
    return readKeyCode(q);
}

std::optional<KeyCodeRecord> Database::findByNativeVk(const QString& kind, int nativeVk) const {
    if (!isOpen()) {
        spdlog::error("[db] findByNativeVk: database not open");
        return std::nullopt;
    }
    QSqlQuery q(db());
    q.prepare(QString::fromLatin1(kSelectKeyCode) +
              QStringLiteral("WHERE kind = ? AND native_vk = ? LIMIT 1"));
    q.addBindValue(kind);
    q.addBindValue(nativeVk);
    if (!q.exec()) {
        spdlog::error("[db] findByNativeVk failed: {}", q.lastError().text().toStdString());
        return std::nullopt;
    }
    return readKeyCode(q);
}

std::optional<KeyCodeRecord> Database::findByNativeHid(int usagePage, int usage) const {
    if (!isOpen()) {
        spdlog::error("[db] findByNativeHid: database not open");
        return std::nullopt;
    }
    QSqlQuery q(db());
    q.prepare(QString::fromLatin1(kSelectKeyCode) +
              QStringLiteral("WHERE native_usage_page = ? AND native_usage = ? LIMIT 1"));
    q.addBindValue(usagePage);
    q.addBindValue(usage);
    if (!q.exec()) {
        spdlog::error("[db] findByNativeHid failed: {}", q.lastError().text().toStdString());
        return std::nullopt;
    }
    return readKeyCode(q);
}

bool Database::migrate() {
    for (const char* sql : kCreateStatements) {
        if (!execSql(QString::fromUtf8(sql))) {
            return false;
        }
    }
    QSqlQuery q(db());
    if (!q.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1"))) {
        spdlog::error("[db] read schema_version failed: {}", q.lastError().text().toStdString());
        return false;
    }
    if (!q.next()) {
        QSqlQuery ins(db());
        ins.prepare(QStringLiteral("INSERT INTO schema_version (version) VALUES (?)"));
        ins.addBindValue(kSchemaVersion);
        if (!ins.exec()) {
            spdlog::error("[db] insert schema_version failed: {}", ins.lastError().text().toStdString());
            return false;
        }
        spdlog::info("[db] schema initialized version={}", kSchemaVersion);
        return true;
    }
    const int version = q.value(0).toInt();
    if (version > kSchemaVersion) {
        spdlog::error("[db] schema version {} newer than binary {}", version, kSchemaVersion);
        return false;
    }
    spdlog::info("[db] schema version={}", version);
    return true;
}

bool Database::seedBuiltins() {
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT OR IGNORE INTO key_codes "
        "(key_id, kind, value_kind, range_min, range_max, native_vk, default_label, origin) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, 'builtin')"));
    int inserted = 0;
    for (const auto& row : kBuiltins) {
        q.bindValue(0, QLatin1String(row.keyId));
        q.bindValue(1, QLatin1String(row.kind));
        q.bindValue(2, QLatin1String(row.valueKind));
        q.bindValue(3, row.min ? QVariant(*row.min) : QVariant());
        q.bindValue(4, row.max ? QVariant(*row.max) : QVariant());
        q.bindValue(5, row.nativeVk ? QVariant(*row.nativeVk) : QVariant());
        q.bindValue(6, QLatin1String(row.label));
        if (!q.exec()) {
            spdlog::error("[db] seed {} failed: {}", row.keyId, q.lastError().text().toStdString());
            return false;
        }
        if (q.numRowsAffected() > 0) {
            ++inserted;
        }
    }
    spdlog::info("[db] builtin key_codes ready inserted={}", inserted);
    return true;
}

std::optional<RecordingSession> Database::beginRecording(const BeginRecordingRequest& req) {
    if (!isOpen()) {
        spdlog::error("[db] beginRecording: database not open");
        return std::nullopt;
    }
    if (req.fps <= 0) {
        spdlog::error("[db] beginRecording: invalid fps={}", req.fps);
        return std::nullopt;
    }

    const QString appDir = QFileInfo(db().databaseName()).absolutePath();
    const QString recDir = appDir + QStringLiteral("/recordings");
    if (!QDir().mkpath(recDir)) {
        spdlog::error("[db] cannot create recordings dir {}", recDir.toStdString());
        return std::nullopt;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 startMs = now.toMSecsSinceEpoch();
    QString display = req.displayName.trimmed();
    if (display.isEmpty()) {
        display = now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    }
    const QString relative =
        QStringLiteral("recordings/%1.bin").arg(now.toString(QStringLiteral("yyyyMMdd-HHmmss-zzz")));
    const QString absolute = appDir + QLatin1Char('/') + relative;

    QSqlDatabase database = db();
    if (!database.transaction()) {
        spdlog::error("[db] begin transaction failed: {}", database.lastError().text().toStdString());
        return std::nullopt;
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT INTO sessions (display_name, start_time, fps, event_log_path, "
        "recording_config_snapshot, profile_name_snapshot) "
        "VALUES (?, ?, ?, ?, ?, ?)"));
    q.addBindValue(display);
    q.addBindValue(startMs);
    q.addBindValue(req.fps);
    q.addBindValue(relative);
    q.addBindValue(req.recordingConfigJson.isEmpty() ? QStringLiteral("{}")
                                                     : req.recordingConfigJson);
    q.addBindValue(req.profileNameSnapshot);
    if (!q.exec()) {
        spdlog::error("[db] insert session failed: {}", q.lastError().text().toStdString());
        database.rollback();
        return std::nullopt;
    }

    RecordingSession out;
    out.sessionId = q.lastInsertId().toLongLong();
    out.fps = req.fps;
    out.eventLogRelative = relative;
    out.eventLogAbsolute = absolute;

    QFile logFile(absolute);
    if (!logFile.open(QIODevice::WriteOnly)) {
        spdlog::error("[db] create event log failed path={} err={}", absolute.toStdString(),
                      logFile.errorString().toStdString());
        database.rollback();
        return std::nullopt;
    }
    logFile.close();

    if (!database.commit()) {
        spdlog::error("[db] commit session failed: {}", database.lastError().text().toStdString());
        return std::nullopt;
    }

    spdlog::info("[db] recording started id={} fps={} log={}", out.sessionId, out.fps,
                 relative.toStdString());
    return out;
}

bool Database::finishRecording(qint64 sessionId, qint64 endTimeMs, qint64 totalEvents,
                               int totalFrames) {
    if (!isOpen()) {
        spdlog::error("[db] finishRecording: database not open");
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "UPDATE sessions SET end_time = ?, total_events = ?, total_frames = ?, "
        "updated_at = strftime('%s', 'now') WHERE id = ? AND end_time IS NULL"));
    q.addBindValue(endTimeMs);
    q.addBindValue(totalEvents);
    q.addBindValue(totalFrames);
    q.addBindValue(sessionId);
    if (!q.exec()) {
        spdlog::error("[db] finishRecording failed: {}", q.lastError().text().toStdString());
        return false;
    }
    if (q.numRowsAffected() == 0) {
        spdlog::warn("[db] finishRecording id={} not found or already finished", sessionId);
        return false;
    }
    spdlog::info("[db] recording finished id={} events={} frames={}", sessionId, totalEvents,
                 totalFrames);
    return true;
}

bool Database::addMarker(qint64 sessionId, quint32 frameIndex, const QString& name) {
    if (!isOpen()) {
        spdlog::error("[db] addMarker: database not open");
        return false;
    }
    if (name.trimmed().isEmpty()) {
        spdlog::warn("[db] addMarker empty name");
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT INTO markers (session_id, frame_index, name) VALUES (?, ?, ?)"));
    q.addBindValue(sessionId);
    q.addBindValue(static_cast<qint64>(frameIndex));
    q.addBindValue(name);
    if (!q.exec()) {
        spdlog::error("[db] addMarker failed: {}", q.lastError().text().toStdString());
        return false;
    }
    spdlog::info("[db] marker session={} frame={} name={}", sessionId, frameIndex,
                 name.toStdString());
    return true;
}

std::optional<int> Database::bindSessionControl(qint64 sessionId, const QString& keyId) {
    if (!isOpen()) {
        spdlog::error("[db] bindSessionControl: database not open");
        return std::nullopt;
    }
    QSqlQuery find(db());
    find.prepare(QStringLiteral(
        "SELECT id, value_kind FROM key_codes WHERE key_id = ?"));
    find.addBindValue(keyId);
    if (!find.exec() || !find.next()) {
        spdlog::warn("[db] bindSessionControl unknown key_id={}", keyId.toStdString());
        return std::nullopt;
    }
    const qint64 codeId = find.value(0).toLongLong();
    const QString valueKind = find.value(1).toString();
    const bool analog = valueKind == QLatin1String("analog");
    const char* table = analog ? "session_axes" : "session_keys";
    const char* indexCol = analog ? "axis_index" : "key_index";

    QSqlQuery existing(db());
    existing.prepare(QStringLiteral("SELECT %1 FROM %2 WHERE session_id = ? AND key_code_id = ?")
                         .arg(QLatin1String(indexCol), QLatin1String(table)));
    existing.addBindValue(sessionId);
    existing.addBindValue(codeId);
    if (!existing.exec()) {
        spdlog::error("[db] bind lookup failed: {}", existing.lastError().text().toStdString());
        return std::nullopt;
    }
    if (existing.next()) {
        return existing.value(0).toInt();
    }

    QSqlQuery next(db());
    next.prepare(QStringLiteral("SELECT COALESCE(MAX(%1), -1) + 1 FROM %2 WHERE session_id = ?")
                     .arg(QLatin1String(indexCol), QLatin1String(table)));
    next.addBindValue(sessionId);
    if (!next.exec() || !next.next()) {
        spdlog::error("[db] next index failed: {}", next.lastError().text().toStdString());
        return std::nullopt;
    }
    const int index = next.value(0).toInt();

    QSqlQuery ins(db());
    ins.prepare(QStringLiteral("INSERT INTO %1 (session_id, %2, key_code_id) VALUES (?, ?, ?)")
                    .arg(QLatin1String(table), QLatin1String(indexCol)));
    ins.addBindValue(sessionId);
    ins.addBindValue(index);
    ins.addBindValue(codeId);
    if (!ins.exec()) {
        spdlog::error("[db] bind insert failed: {}", ins.lastError().text().toStdString());
        return std::nullopt;
    }
    spdlog::info("[db] bound {} key_id={} index={}", analog ? "axis" : "key",
                 keyId.toStdString(), index);
    return index;
}

std::optional<qint64> Database::ensureCaptureKey(const QString& keyId,
                                                 const QString& kind,
                                                 const QString& valueKind,
                                                 const QString& label,
                                                 std::optional<double> rangeMin,
                                                 std::optional<double> rangeMax,
                                                 std::optional<int> nativeVk,
                                                 std::optional<int> nativeUsagePage,
                                                 std::optional<int> nativeUsage) {
    if (!isOpen()) {
        spdlog::error("[db] ensureCaptureKey: database not open");
        return std::nullopt;
    }
    QSqlQuery find(db());
    find.prepare(QStringLiteral("SELECT id FROM key_codes WHERE key_id = ?"));
    find.addBindValue(keyId);
    if (!find.exec()) {
        spdlog::error("[db] ensureCaptureKey lookup failed: {}",
                      find.lastError().text().toStdString());
        return std::nullopt;
    }
    if (find.next()) {
        return find.value(0).toLongLong();
    }
    if (nativeVk) {
        if (const auto byVk = findByNativeVk(kind, *nativeVk)) {
            return byVk->id;
        }
    }
    if (nativeUsagePage && nativeUsage) {
        if (const auto byHid = findByNativeHid(*nativeUsagePage, *nativeUsage)) {
            return byHid->id;
        }
    }
    if (valueKind == QLatin1String("analog") && (!rangeMin || !rangeMax)) {
        spdlog::error("[db] analog capture key {} needs range_min/max", keyId.toStdString());
        return std::nullopt;
    }

    QSqlQuery ins(db());
    ins.prepare(QStringLiteral(
        "INSERT INTO key_codes (key_id, kind, value_kind, range_min, range_max, "
        "native_usage_page, native_usage, native_vk, default_label, origin) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, 'capture')"));
    ins.addBindValue(keyId);
    ins.addBindValue(kind);
    ins.addBindValue(valueKind);
    ins.addBindValue(rangeMin ? QVariant(*rangeMin) : QVariant());
    ins.addBindValue(rangeMax ? QVariant(*rangeMax) : QVariant());
    ins.addBindValue(nativeUsagePage ? QVariant(*nativeUsagePage) : QVariant());
    ins.addBindValue(nativeUsage ? QVariant(*nativeUsage) : QVariant());
    ins.addBindValue(nativeVk ? QVariant(*nativeVk) : QVariant());
    ins.addBindValue(label);
    if (!ins.exec()) {
        spdlog::error("[db] ensureCaptureKey insert failed: {}",
                      ins.lastError().text().toStdString());
        return std::nullopt;
    }
    const qint64 id = ins.lastInsertId().toLongLong();
    spdlog::info("[db] capture key_id={} id={}", keyId.toStdString(), id);
    return id;
}

QByteArray Database::packDigitalBlob(int keyCount, const std::vector<int>& pressedIndices) {
    if (keyCount <= 0) {
        return {};
    }
    // 小端 bit：index 0 → 第 0 字节 bit0。blob 里不放 VK。
    const int nbytes = (keyCount + 7) / 8;
    QByteArray blob(nbytes, '\0');
    auto* bytes = reinterpret_cast<unsigned char*>(blob.data());
    for (const int idx : pressedIndices) {
        if (idx < 0 || idx >= keyCount) {
            continue;
        }
        bytes[idx / 8] = static_cast<unsigned char>(bytes[idx / 8] | (1u << (idx % 8)));
    }
    return blob;
}

QByteArray Database::packAnalogBlob(const std::vector<float>& normalizedValues) {
    if (normalizedValues.empty()) {
        return {};
    }
    QByteArray blob(static_cast<int>(normalizedValues.size() * sizeof(float)), Qt::Uninitialized);
    std::memcpy(blob.data(), normalizedValues.data(), static_cast<size_t>(blob.size()));
    return blob;
}

bool Database::appendDigitalRun(qint64 sessionId, int startFrame, int runLen,
                                const QByteArray& stateBlob) {
    if (!isOpen()) {
        spdlog::error("[db] appendDigitalRun: database not open");
        return false;
    }
    if (runLen < 1 || stateBlob.isEmpty() || startFrame < 0) {
        spdlog::warn("[db] appendDigitalRun invalid session={} start={} len={} blob={}",
                     sessionId, startFrame, runLen, stateBlob.size());
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT INTO frame_data (session_id, start_frame, run_len, state_blob) "
        "VALUES (?, ?, ?, ?)"));
    q.addBindValue(sessionId);
    q.addBindValue(startFrame);
    q.addBindValue(runLen);
    q.addBindValue(stateBlob);
    if (!q.exec()) {
        spdlog::error("[db] appendDigitalRun failed: {}", q.lastError().text().toStdString());
        return false;
    }
    return true;
}

bool Database::appendAnalogRun(qint64 sessionId, int startFrame, int runLen,
                               const QByteArray& valuesBlob) {
    if (!isOpen()) {
        spdlog::error("[db] appendAnalogRun: database not open");
        return false;
    }
    if (runLen < 1 || valuesBlob.isEmpty() || startFrame < 0) {
        spdlog::warn("[db] appendAnalogRun invalid session={} start={} len={} blob={}",
                     sessionId, startFrame, runLen, valuesBlob.size());
        return false;
    }
    if ((valuesBlob.size() % static_cast<int>(sizeof(float))) != 0) {
        spdlog::error("[db] appendAnalogRun blob size {} not multiple of float32",
                      valuesBlob.size());
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "INSERT INTO axis_samples (session_id, start_frame, run_len, values_blob) "
        "VALUES (?, ?, ?, ?)"));
    q.addBindValue(sessionId);
    q.addBindValue(startFrame);
    q.addBindValue(runLen);
    q.addBindValue(valuesBlob);
    if (!q.exec()) {
        spdlog::error("[db] appendAnalogRun failed: {}", q.lastError().text().toStdString());
        return false;
    }
    return true;
}
