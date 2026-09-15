#include "Database.h"
#include "RecordingLayout.h"

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

// 表结构见 docs/DatabaseSchema.md。frame_data 一行一帧：frame + blob，批量 INSERT。

namespace {

constexpr int kSchemaVersion = 4;

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
        origin TEXT NOT NULL DEFAULT 'user'
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS sessions (
        id INTEGER PRIMARY KEY AUTOINCREMENT,
        display_name TEXT NOT NULL,
        start_time INTEGER NOT NULL,
        end_time INTEGER,
        fps INTEGER NOT NULL,
        total_frames INTEGER NOT NULL DEFAULT 0,
        device_bits INTEGER NOT NULL,
        note TEXT NOT NULL DEFAULT '',
        tags TEXT NOT NULL DEFAULT ''
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS frame_data (
        session_id INTEGER NOT NULL,
        frame INTEGER NOT NULL,
        blob BLOB NOT NULL,
        PRIMARY KEY (session_id, frame),
        FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
    ))SQL",
    R"SQL(CREATE TABLE IF NOT EXISTS markers (
        session_id INTEGER NOT NULL,
        frame INTEGER NOT NULL,
        name TEXT NOT NULL,
        note TEXT NOT NULL DEFAULT '',
        PRIMARY KEY (session_id, frame, name),
        FOREIGN KEY (session_id) REFERENCES sessions(id) ON DELETE CASCADE
    ))SQL",
    "CREATE INDEX IF NOT EXISTS idx_sessions_start ON sessions(start_time DESC)",
    "CREATE INDEX IF NOT EXISTS idx_key_codes_native_vk ON key_codes(kind, native_vk)",
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
    {"space", "keyboard", "digital", "Space", std::nullopt, std::nullopt, 0x20},
    {"shift-left", "keyboard", "digital", "Shift L", std::nullopt, std::nullopt, 0xA0},
    {"shift-right", "keyboard", "digital", "Shift R", std::nullopt, std::nullopt, 0xA1},
    {"ctrl-left", "keyboard", "digital", "Ctrl L", std::nullopt, std::nullopt, 0xA2},
    {"ctrl-right", "keyboard", "digital", "Ctrl R", std::nullopt, std::nullopt, 0xA3},
    {"alt-left", "keyboard", "digital", "Alt L", std::nullopt, std::nullopt, 0xA4},
    {"alt-right", "keyboard", "digital", "Alt R", std::nullopt, std::nullopt, 0xA5},
    {"tab", "keyboard", "digital", "Tab", std::nullopt, std::nullopt, 0x09},
    {"caps-lock", "keyboard", "digital", "Caps", std::nullopt, std::nullopt, 0x14},
    {"escape", "keyboard", "digital", "Esc", std::nullopt, std::nullopt, 0x1B},
    {"enter", "keyboard", "digital", "Enter", std::nullopt, std::nullopt, 0x0D},
    {"backspace", "keyboard", "digital", "Backspace", std::nullopt, std::nullopt, 0x08},
    {"win-left", "keyboard", "digital", "Win L", std::nullopt, std::nullopt, 0x5B},
    {"win-right", "keyboard", "digital", "Win R", std::nullopt, std::nullopt, 0x5C},
    {"menu", "keyboard", "digital", "Menu", std::nullopt, std::nullopt, 0x5D},
    {"insert", "keyboard", "digital", "Ins", std::nullopt, std::nullopt, 0x2D},
    {"delete", "keyboard", "digital", "Del", std::nullopt, std::nullopt, 0x2E},
    {"home", "keyboard", "digital", "Home", std::nullopt, std::nullopt, 0x24},
    {"end", "keyboard", "digital", "End", std::nullopt, std::nullopt, 0x23},
    {"page-up", "keyboard", "digital", "PgUp", std::nullopt, std::nullopt, 0x21},
    {"page-down", "keyboard", "digital", "PgDn", std::nullopt, std::nullopt, 0x22},
    {"arrow-left", "keyboard", "digital", "Left", std::nullopt, std::nullopt, 0x25},
    {"arrow-up", "keyboard", "digital", "Up", std::nullopt, std::nullopt, 0x26},
    {"arrow-right", "keyboard", "digital", "Right", std::nullopt, std::nullopt, 0x27},
    {"arrow-down", "keyboard", "digital", "Down", std::nullopt, std::nullopt, 0x28},
    {"print-screen", "keyboard", "digital", "PrtSc", std::nullopt, std::nullopt, 0x2C},
    {"scroll-lock", "keyboard", "digital", "ScrLk", std::nullopt, std::nullopt, 0x91},
    {"pause", "keyboard", "digital", "Pause", std::nullopt, std::nullopt, 0x13},
    {"num-lock", "keyboard", "digital", "NumLk", std::nullopt, std::nullopt, 0x90},
    {"semicolon", "keyboard", "digital", ";", std::nullopt, std::nullopt, 0xBA},
    {"equal", "keyboard", "digital", "=", std::nullopt, std::nullopt, 0xBB},
    {"comma", "keyboard", "digital", ",", std::nullopt, std::nullopt, 0xBC},
    {"minus", "keyboard", "digital", "-", std::nullopt, std::nullopt, 0xBD},
    {"period", "keyboard", "digital", ".", std::nullopt, std::nullopt, 0xBE},
    {"slash", "keyboard", "digital", "/", std::nullopt, std::nullopt, 0xBF},
    {"grave", "keyboard", "digital", "`", std::nullopt, std::nullopt, 0xC0},
    {"lbracket", "keyboard", "digital", "[", std::nullopt, std::nullopt, 0xDB},
    {"backslash", "keyboard", "digital", "\\", std::nullopt, std::nullopt, 0xDC},
    {"rbracket", "keyboard", "digital", "]", std::nullopt, std::nullopt, 0xDD},
    {"quote", "keyboard", "digital", "'", std::nullopt, std::nullopt, 0xDE},
    {"numpad-0", "keyboard", "digital", "Num0", std::nullopt, std::nullopt, 0x60},
    {"numpad-1", "keyboard", "digital", "Num1", std::nullopt, std::nullopt, 0x61},
    {"numpad-2", "keyboard", "digital", "Num2", std::nullopt, std::nullopt, 0x62},
    {"numpad-3", "keyboard", "digital", "Num3", std::nullopt, std::nullopt, 0x63},
    {"numpad-4", "keyboard", "digital", "Num4", std::nullopt, std::nullopt, 0x64},
    {"numpad-5", "keyboard", "digital", "Num5", std::nullopt, std::nullopt, 0x65},
    {"numpad-6", "keyboard", "digital", "Num6", std::nullopt, std::nullopt, 0x66},
    {"numpad-7", "keyboard", "digital", "Num7", std::nullopt, std::nullopt, 0x67},
    {"numpad-8", "keyboard", "digital", "Num8", std::nullopt, std::nullopt, 0x68},
    {"numpad-9", "keyboard", "digital", "Num9", std::nullopt, std::nullopt, 0x69},
    {"numpad-mul", "keyboard", "digital", "Num*", std::nullopt, std::nullopt, 0x6A},
    {"numpad-add", "keyboard", "digital", "Num+", std::nullopt, std::nullopt, 0x6B},
    {"numpad-sub", "keyboard", "digital", "Num-", std::nullopt, std::nullopt, 0x6D},
    {"numpad-dot", "keyboard", "digital", "Num.", std::nullopt, std::nullopt, 0x6E},
    {"numpad-div", "keyboard", "digital", "Num/", std::nullopt, std::nullopt, 0x6F},
    {"mouse-left", "mouse", "digital", "LMB", std::nullopt, std::nullopt, 0x01},
    {"mouse-right", "mouse", "digital", "RMB", std::nullopt, std::nullopt, 0x02},
    {"mouse-middle", "mouse", "digital", "MMB", std::nullopt, std::nullopt, 0x04},
    {"mouse-x1", "mouse", "digital", "X1", std::nullopt, std::nullopt, 0x05},
    {"mouse-x2", "mouse", "digital", "X2", std::nullopt, std::nullopt, 0x06},
    {"mouse-dx", "mouse", "analog", "Mouse DX", -1.0, 1.0, std::nullopt},
    {"mouse-dy", "mouse", "analog", "Mouse DY", -1.0, 1.0, std::nullopt},
    {"pad-a", "gamepad", "digital", "A", std::nullopt, std::nullopt, std::nullopt},
    {"pad-b", "gamepad", "digital", "B", std::nullopt, std::nullopt, std::nullopt},
    {"pad-x", "gamepad", "digital", "X", std::nullopt, std::nullopt, std::nullopt},
    {"pad-y", "gamepad", "digital", "Y", std::nullopt, std::nullopt, std::nullopt},
    {"pad-lb", "gamepad", "digital", "LB", std::nullopt, std::nullopt, std::nullopt},
    {"pad-rb", "gamepad", "digital", "RB", std::nullopt, std::nullopt, std::nullopt},
    {"pad-start", "gamepad", "digital", "Start", std::nullopt, std::nullopt, std::nullopt},
    {"pad-back", "gamepad", "digital", "Back", std::nullopt, std::nullopt, std::nullopt},
    {"pad-ls", "gamepad", "digital", "LS", std::nullopt, std::nullopt, std::nullopt},
    {"pad-rs", "gamepad", "digital", "RS", std::nullopt, std::nullopt, std::nullopt},
    {"pad-up", "gamepad", "digital", "Up", std::nullopt, std::nullopt, std::nullopt},
    {"pad-down", "gamepad", "digital", "Down", std::nullopt, std::nullopt, std::nullopt},
    {"pad-left", "gamepad", "digital", "Left", std::nullopt, std::nullopt, std::nullopt},
    {"pad-right", "gamepad", "digital", "Right", std::nullopt, std::nullopt, std::nullopt},
    {"pad-lt", "gamepad", "analog", "LT", 0.0, 1.0, std::nullopt},
    {"pad-rt", "gamepad", "analog", "RT", 0.0, 1.0, std::nullopt},
    {"pad-lx", "gamepad", "analog", "Left X", -1.0, 1.0, std::nullopt},
    {"pad-ly", "gamepad", "analog", "Left Y", -1.0, 1.0, std::nullopt},
    {"pad-rx", "gamepad", "analog", "Right X", -1.0, 1.0, std::nullopt},
    {"pad-ry", "gamepad", "analog", "Right Y", -1.0, 1.0, std::nullopt},
};

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

KeyCodeRecord fillKeyCode(const QSqlQuery& q) {
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

std::optional<KeyCodeRecord> readKeyCode(QSqlQuery& q) {
    if (!q.next()) {
        return std::nullopt;
    }
    return fillKeyCode(q);
}

bool isKnownKind(const QString& kind) {
    return kind == QLatin1String("keyboard") || kind == QLatin1String("mouse") ||
           kind == QLatin1String("gamepad") || kind == QLatin1String("other");
}

bool isKnownValueKind(const QString& valueKind) {
    return valueKind == QLatin1String("digital") || valueKind == QLatin1String("analog");
}

const char* kSelectKeyCode =
    "SELECT id, key_id, kind, value_kind, range_min, range_max, "
    "native_usage_page, native_usage, native_vk, default_label, origin "
    "FROM key_codes ";

} // namespace

Database::Database(QString connectionName) : connectionName_(std::move(connectionName)) {
    if (connectionName_.isEmpty()) {
        connectionName_ = QStringLiteral("pc");
    }
}

QSqlDatabase Database::db() const {
    return QSqlDatabase::database(connectionName_);
}

bool Database::execSql(const QString& sql) {
    QSqlQuery q(db());
    if (!q.exec(sql)) {
        spdlog::error("[db] exec failed: {} sql={}", q.lastError().text().toStdString(),
                      sql.left(80).toStdString());
        return false;
    }
    q.finish();
    return true;
}

bool Database::connectToFile(const QString& dbFilePath) {
    if (QSqlDatabase::contains(connectionName_)) {
        QSqlDatabase::removeDatabase(connectionName_);
    }
    if (!QSqlDatabase::isDriverAvailable(QStringLiteral("QSQLITE"))) {
        spdlog::critical("[db] Qt QSQLITE driver missing");
        return false;
    }

    QDir().mkpath(QFileInfo(dbFilePath).absolutePath());
    auto database = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), connectionName_);
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
        pragma.finish();
        // journal_mode 会返回一行；不 next/finish 的话语句还占着连接，后面 DROP 会 locked。
        if (!pragma.exec(QStringLiteral("PRAGMA journal_mode = WAL"))) {
            spdlog::warn("[db] PRAGMA journal_mode=WAL failed: {}",
                         pragma.lastError().text().toStdString());
        } else {
            pragma.next();
        }
        pragma.finish();
    }
    spdlog::info("[db] opened {} conn={}", dbFilePath.toStdString(),
                 connectionName_.toStdString());
    return true;
}

bool Database::open(const QString& dbFilePath) {
    filePath_ = dbFilePath;
    if (!connectToFile(dbFilePath)) {
        return false;
    }
    if (!migrate() || !seedBuiltins()) {
        return false;
    }
    return true;
}

void Database::close() {
    if (!QSqlDatabase::contains(connectionName_)) {
        return;
    }
    {
        auto database = db();
        if (database.isOpen()) {
            database.close();
        }
    }
    QSqlDatabase::removeDatabase(connectionName_);
    spdlog::info("[db] closed conn={}", connectionName_.toStdString());
}

bool Database::isOpen() const {
    return QSqlDatabase::contains(connectionName_) && db().isOpen();
}

namespace {

int countTable(QSqlDatabase database, const char* table) {
    QSqlQuery q(database);
    if (!q.exec(QStringLiteral("SELECT COUNT(*) FROM %1").arg(QLatin1String(table))) || !q.next()) {
        return -1;
    }
    return q.value(0).toInt();
}

bool removeSqliteFiles(const QString& path) {
    bool ok = true;
    const QString files[] = {
        path,
        path + QStringLiteral("-wal"),
        path + QStringLiteral("-shm"),
    };
    for (const auto& file : files) {
        if (!QFile::exists(file)) {
            continue;
        }
        if (!QFile::remove(file)) {
            spdlog::warn("[db] recreate cannot delete {}", file.toStdString());
            ok = false;
        }
    }
    return ok;
}

} // namespace

Database::Stats Database::stats() const {
    Stats out;
    out.path = filePath_;
    out.open = isOpen();
    if (!out.open) {
        return out;
    }
    QSqlQuery q(db());
    if (q.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1")) && q.next()) {
        out.schemaVersion = q.value(0).toInt();
    }
    out.keyCodes = countTable(db(), "key_codes");
    out.sessions = countTable(db(), "sessions");
    out.frames = countTable(db(), "frame_data");
    out.markers = countTable(db(), "markers");
    return out;
}

bool Database::recreate() {
    if (filePath_.isEmpty()) {
        spdlog::error("[db] recreate: no path");
        return false;
    }
    const QString path = filePath_;
    // WAL 打开时 Windows 锁着文件，必须先 close / removeDatabase。
    close();
    const bool filesGone = removeSqliteFiles(path);
    if (!filesGone) {
        spdlog::warn("[db] recreate files locked, DROP tables instead path={}", path.toStdString());
        if (!open(path) || !resetSchema()) {
            return false;
        }
        close();
    } else {
        spdlog::warn("[db] recreate deleted sqlite files path={}", path.toStdString());
    }
    const bool ok = open(path);
    if (ok) {
        spdlog::info("[db] recreate done path={}", path.toStdString());
    }
    return ok;
}

bool Database::isValidKeyId(const QString& keyId) {
    if (keyId.isEmpty()) {
        return false;
    }
    bool prevHyphen = false;
    for (int i = 0; i < keyId.size(); ++i) {
        const QChar c = keyId[i];
        const bool alnum = (c >= u'a' && c <= u'z') || (c >= u'0' && c <= u'9');
        if (alnum) {
            prevHyphen = false;
            continue;
        }
        // 不能开头/结尾/连续 '-'，与 builtin 的 mouse-left 一致。
        if (c == u'-' && i > 0 && i + 1 < keyId.size() && !prevHyphen) {
            prevHyphen = true;
            continue;
        }
        return false;
    }
    return !prevHyphen;
}

std::optional<KeyCodeRecord> Database::findById(qint64 id) const {
    if (!isOpen()) {
        spdlog::error("[db] findById: database not open");
        return std::nullopt;
    }
    QSqlQuery q(db());
    q.prepare(QString::fromLatin1(kSelectKeyCode) + QStringLiteral("WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        spdlog::error("[db] findById failed: {}", q.lastError().text().toStdString());
        return std::nullopt;
    }
    return readKeyCode(q);
}

std::vector<KeyCodeRecord> Database::listKeyCodes() const {
    std::vector<KeyCodeRecord> out;
    if (!isOpen()) {
        spdlog::error("[db] listKeyCodes: database not open");
        return out;
    }
    QSqlQuery q(db());
    if (!q.exec(QString::fromLatin1(kSelectKeyCode) + QStringLiteral("ORDER BY kind, key_id"))) {
        spdlog::error("[db] listKeyCodes failed: {}", q.lastError().text().toStdString());
        return out;
    }
    while (q.next()) {
        out.push_back(fillKeyCode(q));
    }
    return out;
}

std::optional<qint64> Database::insertUserKey(const QString& keyId, const QString& kind,
                                              const QString& valueKind, const QString& label,
                                              std::optional<double> rangeMin,
                                              std::optional<double> rangeMax) {
    if (!isOpen()) {
        spdlog::error("[db] insertUserKey: database not open");
        return std::nullopt;
    }
    const QString id = keyId.trimmed().toLower();
    const QString kindNorm = kind.trimmed().toLower();
    const QString valueNorm = valueKind.trimmed().toLower();
    const QString labelNorm = label.trimmed();
    if (!isValidKeyId(id)) {
        spdlog::warn("[db] insertUserKey: invalid key_id={}", keyId.toStdString());
        return std::nullopt;
    }
    if (!isKnownKind(kindNorm) || !isKnownValueKind(valueNorm)) {
        spdlog::warn("[db] insertUserKey: bad kind/value_kind key_id={}", id.toStdString());
        return std::nullopt;
    }
    if (labelNorm.isEmpty()) {
        spdlog::warn("[db] insertUserKey: empty label key_id={}", id.toStdString());
        return std::nullopt;
    }
    std::optional<double> min = rangeMin;
    std::optional<double> max = rangeMax;
    if (valueNorm == QLatin1String("analog")) {
        if (!min || !max || *min >= *max) {
            spdlog::warn("[db] insertUserKey: analog {} needs range_min < range_max",
                         id.toStdString());
            return std::nullopt;
        }
    } else {
        min.reset();
        max.reset();
    }

    QSqlQuery ins(db());
    ins.prepare(QStringLiteral(
        "INSERT INTO key_codes (key_id, kind, value_kind, range_min, range_max, "
        "default_label, origin) VALUES (?, ?, ?, ?, ?, ?, 'user')"));
    ins.addBindValue(id);
    ins.addBindValue(kindNorm);
    ins.addBindValue(valueNorm);
    ins.addBindValue(min ? QVariant(*min) : QVariant());
    ins.addBindValue(max ? QVariant(*max) : QVariant());
    ins.addBindValue(labelNorm);
    if (!ins.exec()) {
        spdlog::error("[db] insertUserKey failed key_id={} err={}", id.toStdString(),
                      ins.lastError().text().toStdString());
        return std::nullopt;
    }
    const qint64 rowId = ins.lastInsertId().toLongLong();
    spdlog::info("[db] user key_id={} id={}", id.toStdString(), rowId);
    return rowId;
}

bool Database::updateKeyMeta(qint64 id, const QString& label, std::optional<double> rangeMin,
                             std::optional<double> rangeMax) {
    if (!isOpen()) {
        spdlog::error("[db] updateKeyMeta: database not open");
        return false;
    }
    const auto row = findById(id);
    if (!row) {
        spdlog::warn("[db] updateKeyMeta: id={} not found", id);
        return false;
    }
    const QString labelNorm = label.trimmed();
    if (labelNorm.isEmpty()) {
        spdlog::warn("[db] updateKeyMeta: empty label id={}", id);
        return false;
    }
    std::optional<double> min = rangeMin;
    std::optional<double> max = rangeMax;
    if (row->valueKind == QLatin1String("analog")) {
        if (!min || !max || *min >= *max) {
            spdlog::warn("[db] updateKeyMeta: analog id={} needs range_min < range_max", id);
            return false;
        }
    } else {
        min.reset();
        max.reset();
    }

    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "UPDATE key_codes SET default_label = ?, range_min = ?, range_max = ? WHERE id = ?"));
    q.addBindValue(labelNorm);
    q.addBindValue(min ? QVariant(*min) : QVariant());
    q.addBindValue(max ? QVariant(*max) : QVariant());
    q.addBindValue(id);
    if (!q.exec()) {
        spdlog::error("[db] updateKeyMeta failed id={} err={}", id,
                      q.lastError().text().toStdString());
        return false;
    }
    spdlog::info("[db] updateKeyMeta id={} key_id={}", id, row->keyId.toStdString());
    return true;
}

bool Database::bindNativeVk(qint64 id, int nativeVk) {
    if (!isOpen()) {
        spdlog::error("[db] bindNativeVk: database not open");
        return false;
    }
    if (nativeVk <= 0) {
        spdlog::warn("[db] bindNativeVk: invalid native_vk={} id={}", nativeVk, id);
        return false;
    }
    const auto row = findById(id);
    if (!row) {
        spdlog::warn("[db] bindNativeVk: id={} not found", id);
        return false;
    }

    auto database = db();
    if (!database.transaction()) {
        spdlog::error("[db] bindNativeVk: begin transaction failed");
        return false;
    }
    QSqlQuery clear(database);
    clear.prepare(QStringLiteral(
        "UPDATE key_codes SET native_vk = NULL WHERE kind = ? AND native_vk = ? AND id != ?"));
    clear.addBindValue(row->kind);
    clear.addBindValue(nativeVk);
    clear.addBindValue(id);
    if (!clear.exec()) {
        spdlog::error("[db] bindNativeVk clear failed: {}",
                      clear.lastError().text().toStdString());
        database.rollback();
        return false;
    }
    const int cleared = clear.numRowsAffected();
    QSqlQuery bind(database);
    bind.prepare(QStringLiteral("UPDATE key_codes SET native_vk = ? WHERE id = ?"));
    bind.addBindValue(nativeVk);
    bind.addBindValue(id);
    if (!bind.exec()) {
        spdlog::error("[db] bindNativeVk update failed: {}",
                      bind.lastError().text().toStdString());
        database.rollback();
        return false;
    }
    if (!database.commit()) {
        spdlog::error("[db] bindNativeVk commit failed: {}",
                      database.lastError().text().toStdString());
        database.rollback();
        return false;
    }
    spdlog::info("[db] bind native_vk={} key_id={} id={} cleared={}", nativeVk,
                 row->keyId.toStdString(), id, cleared);
    return true;
}

bool Database::deleteKeyCode(qint64 id) {
    if (!isOpen()) {
        spdlog::error("[db] deleteKeyCode: database not open");
        return false;
    }
    const auto row = findById(id);
    if (!row) {
        spdlog::warn("[db] deleteKeyCode: id={} not found", id);
        return false;
    }
    if (row->origin == QLatin1String("builtin")) {
        spdlog::warn("[db] deleteKeyCode: refuse builtin key_id={}", row->keyId.toStdString());
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral("DELETE FROM key_codes WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        spdlog::error("[db] deleteKeyCode failed id={} err={}", id,
                      q.lastError().text().toStdString());
        return false;
    }
    spdlog::info("[db] deleted key_id={} origin={} id={}", row->keyId.toStdString(),
                 row->origin.toStdString(), id);
    return true;
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

bool Database::resetSchema() {
    spdlog::warn("[db] dropping old schema, all sessions discarded");
    const char* drops[] = {
        "DROP TABLE IF EXISTS session_tags",
        "DROP TABLE IF EXISTS tags",
        "DROP TABLE IF EXISTS markers",
        "DROP TABLE IF EXISTS axis_samples",
        "DROP TABLE IF EXISTS frame_data",
        "DROP TABLE IF EXISTS session_axes",
        "DROP TABLE IF EXISTS session_keys",
        "DROP TABLE IF EXISTS sessions",
        "DROP TABLE IF EXISTS key_codes",
        "DROP TABLE IF EXISTS schema_version",
    };
    for (const char* sql : drops) {
        if (!execSql(QString::fromUtf8(sql))) {
            return false;
        }
    }
    return true;
}

int Database::readSchemaVersion() const {
    int version = 0;
    {
        QSqlQuery q(db());
        if (q.exec(QStringLiteral("SELECT version FROM schema_version LIMIT 1")) && q.next()) {
            version = q.value(0).toInt();
        }
        q.finish();
    }
    return version;
}

bool Database::migrate() {
    int version = readSchemaVersion();
    if (version > kSchemaVersion) {
        spdlog::error("[db] schema version {} newer than binary {}", version, kSchemaVersion);
        return false;
    }
    if (version != 0 && version != kSchemaVersion) {
        // 旧库不兼容：关连接再删文件。Qt 的 QSqlQuery 不 finish 就 DROP 会 table locked。
        spdlog::warn("[db] schema {} != {}, recreating empty database", version, kSchemaVersion);
        const QString path = filePath_;
        close();
        if (!removeSqliteFiles(path)) {
            spdlog::warn("[db] sqlite files locked, trying DROP TABLE path={}", path.toStdString());
            if (!connectToFile(path) || !resetSchema()) {
                spdlog::error("[db] old schema wipe failed; close any other process using {}",
                              path.toStdString());
                return false;
            }
        } else if (!connectToFile(path)) {
            return false;
        }
        version = 0;
    }

    for (const char* sql : kCreateStatements) {
        if (!execSql(QString::fromUtf8(sql))) {
            return false;
        }
    }
    if (version == 0) {
        QSqlQuery ins(db());
        ins.prepare(QStringLiteral("INSERT INTO schema_version (version) VALUES (?)"));
        ins.addBindValue(kSchemaVersion);
        if (!ins.exec()) {
            spdlog::error("[db] insert schema_version failed: {}",
                          ins.lastError().text().toStdString());
            return false;
        }
        ins.finish();
        spdlog::info("[db] schema initialized version={}", kSchemaVersion);
        return true;
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
    const auto insertRow = [&](const QString& keyId, const char* kind, const char* valueKind,
                               const QVariant& min, const QVariant& max, const QVariant& vk,
                               const QString& label) -> bool {
        q.bindValue(0, keyId);
        q.bindValue(1, QLatin1String(kind));
        q.bindValue(2, QLatin1String(valueKind));
        q.bindValue(3, min);
        q.bindValue(4, max);
        q.bindValue(5, vk);
        q.bindValue(6, label);
        if (!q.exec()) {
            spdlog::error("[db] seed {} failed: {}", keyId.toStdString(),
                          q.lastError().text().toStdString());
            return false;
        }
        if (q.numRowsAffected() > 0) {
            ++inserted;
        }
        return true;
    };

    for (char c = 'a'; c <= 'z'; ++c) {
        const QString id = QString(QLatin1Char(c));
        const QString label = QString(QLatin1Char(static_cast<char>(c - 'a' + 'A')));
        if (!insertRow(id, "keyboard", "digital", QVariant(), QVariant(),
                       0x41 + (c - 'a'), label)) {
            return false;
        }
    }
    for (int digit = 0; digit <= 9; ++digit) {
        const QString id = QString::number(digit);
        if (!insertRow(id, "keyboard", "digital", QVariant(), QVariant(), 0x30 + digit, id)) {
            return false;
        }
    }
    for (int f = 1; f <= 12; ++f) {
        const QString id = QStringLiteral("f%1").arg(f);
        if (!insertRow(id, "keyboard", "digital", QVariant(), QVariant(), 0x70 + (f - 1),
                       id.toUpper())) {
            return false;
        }
    }
    for (const auto& row : kBuiltins) {
        if (!insertRow(QLatin1String(row.keyId), row.kind, row.valueKind,
                       row.min ? QVariant(*row.min) : QVariant(),
                       row.max ? QVariant(*row.max) : QVariant(),
                       row.nativeVk ? QVariant(*row.nativeVk) : QVariant(),
                       QLatin1String(row.label))) {
            return false;
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
    if (req.deviceBits == 0) {
        spdlog::error("[db] beginRecording: device_bits=0");
        return std::nullopt;
    }

    const QDateTime now = QDateTime::currentDateTime();
    const qint64 startMs = now.toMSecsSinceEpoch();
    QString display = req.displayName.trimmed();
    if (display.isEmpty()) {
        display = now.toString(QStringLiteral("yyyyMMdd-HHmmss"));
    }
    QSqlDatabase database = db();
    if (!database.transaction()) {
        spdlog::error("[db] begin transaction failed: {}", database.lastError().text().toStdString());
        return std::nullopt;
    }

    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT INTO sessions (display_name, start_time, fps, device_bits) "
        "VALUES (?, ?, ?, ?)"));
    q.addBindValue(display);
    q.addBindValue(startMs);
    q.addBindValue(req.fps);
    q.addBindValue(static_cast<int>(req.deviceBits));
    if (!q.exec()) {
        spdlog::error("[db] insert session failed: {}", q.lastError().text().toStdString());
        database.rollback();
        return std::nullopt;
    }

    RecordingSession out;
    out.sessionId = q.lastInsertId().toLongLong();
    out.fps = req.fps;
    out.layout.deviceBits = req.deviceBits;
    out.layout.digitalCount = RecLayout::digitalCount(req.deviceBits);
    out.layout.analogCount = RecLayout::analogCount(req.deviceBits);

    if (!database.commit()) {
        spdlog::error("[db] commit session failed: {}", database.lastError().text().toStdString());
        return std::nullopt;
    }

    spdlog::info("[db] recording started id={} fps={} bits={:#x} digital={} analog={}",
                 out.sessionId, out.fps, req.deviceBits, out.layout.digitalCount,
                 out.layout.analogCount);
    return out;
}

bool Database::finishRecording(qint64 sessionId, qint64 endTimeMs, int totalFrames) {
    if (!isOpen()) {
        spdlog::error("[db] finishRecording: database not open");
        return false;
    }
    QSqlQuery q(db());
    q.prepare(QStringLiteral(
        "UPDATE sessions SET end_time = ?, total_frames = ? "
        "WHERE id = ? AND end_time IS NULL"));
    q.addBindValue(endTimeMs);
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
    spdlog::info("[db] recording finished id={} frames={}", sessionId, totalFrames);
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
        "INSERT INTO markers (session_id, frame, name) VALUES (?, ?, ?)"));
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

QByteArray Database::packFrameBlob(const QByteArray& digital, const QByteArray& analog) {
    QByteArray blob;
    blob.reserve(digital.size() + analog.size());
    blob.append(digital);
    blob.append(analog);
    return blob;
}

bool Database::appendFrames(qint64 sessionId, const std::vector<FrameRow>& frames) {
    if (!isOpen()) {
        spdlog::error("[db] appendFrames: database not open");
        return false;
    }
    if (frames.empty()) {
        return true;
    }
    QSqlDatabase database = db();
    if (!database.transaction()) {
        spdlog::error("[db] appendFrames begin failed: {}",
                      database.lastError().text().toStdString());
        return false;
    }
    QSqlQuery q(database);
    q.prepare(QStringLiteral(
        "INSERT INTO frame_data (session_id, frame, blob) VALUES (?, ?, ?)"));
    for (const auto& row : frames) {
        if (row.frame < 0 || row.blob.isEmpty()) {
            spdlog::warn("[db] appendFrames skip frame={} blob={}", row.frame, row.blob.size());
            database.rollback();
            return false;
        }
        q.bindValue(0, sessionId);
        q.bindValue(1, row.frame);
        q.bindValue(2, row.blob);
        if (!q.exec()) {
            spdlog::error("[db] appendFrames insert failed: {}",
                          q.lastError().text().toStdString());
            database.rollback();
            return false;
        }
    }
    if (!database.commit()) {
        spdlog::error("[db] appendFrames commit failed: {}",
                      database.lastError().text().toStdString());
        return false;
    }
    spdlog::debug("[db] appendFrames session={} count={}", sessionId, frames.size());
    return true;
}
