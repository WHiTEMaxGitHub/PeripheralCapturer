#pragma once

#include <QByteArray>
#include <QSqlDatabase>
#include <QString>
#include <cstdint>
#include <optional>
#include <vector>

// SQLite：码本 + 会话 + 一行一帧的 blob。通道顺序：RecLayout + sessions.device_bits。
struct BeginRecordingRequest {
    QString displayName;
    int fps = 60;
    uint16_t deviceBits = 0;
};

struct RecordingLayoutResult {
    uint16_t deviceBits = 0;
    int digitalCount = 0;
    int analogCount = 0;
};

struct RecordingSession {
    qint64 sessionId = 0;
    int fps = 60;
    RecordingLayoutResult layout;
};

struct FrameRow {
    int frame = 0;
    QByteArray blob; // 前半 digital bitset + 后半 analog float32
};

struct KeyCodeRecord {
    qint64 id = 0;
    QString keyId;
    QString kind;
    QString valueKind;
    QString defaultLabel;
    QString origin;
    std::optional<double> rangeMin;
    std::optional<double> rangeMax;
    std::optional<int> nativeUsagePage;
    std::optional<int> nativeUsage;
    std::optional<int> nativeVk;
};

class Database {
public:
    explicit Database(QString connectionName = QStringLiteral("pc"));
    struct Stats {
        QString path;
        bool open = false;
        int schemaVersion = -1;
        int keyCodes = 0;
        int sessions = 0;
        int frames = 0;
        int markers = 0;
    };

    bool open(const QString& dbFilePath);
    void close();
    bool isOpen() const;
    QString filePath() const { return filePath_; }
    Stats stats() const;

    // 先关连接再删 data.db / -wal / -shm，然后 open。WAL 不关就删会失败。
    bool recreate();

    std::optional<KeyCodeRecord> findById(qint64 id) const;
    std::optional<KeyCodeRecord> findByKeyId(const QString& keyId) const;
    std::optional<KeyCodeRecord> findByNativeVk(const QString& kind, int nativeVk) const;
    std::optional<KeyCodeRecord> findByNativeHid(int usagePage, int usage) const;
    std::vector<KeyCodeRecord> listKeyCodes() const;

    // 码本页：origin=user。key_id 小写、字母数字和 '-'。
    std::optional<qint64> insertUserKey(const QString& keyId,
                                        const QString& kind,
                                        const QString& valueKind,
                                        const QString& label,
                                        std::optional<double> rangeMin = std::nullopt,
                                        std::optional<double> rangeMax = std::nullopt);

    // builtin 可改标签/范围，不能改 key_id / value_kind。
    bool updateKeyMeta(qint64 id, const QString& label, std::optional<double> rangeMin,
                       std::optional<double> rangeMax);

    // 同一 kind 上同一个 VK 只留这一行，否则 findByNativeVk LIMIT 1 会绑错。
    bool bindNativeVk(qint64 id, int nativeVk);

    // 只删 user / capture。
    bool deleteKeyCode(qint64 id);

    static bool isValidKeyId(const QString& keyId);

    // 开录：写 sessions 一行，冻 fps 和 device_bits。通道表不进库。
    std::optional<RecordingSession> beginRecording(const BeginRecordingRequest& req);

    // 停录：回填时长与帧数。不要在热路径里每条事件 UPDATE。
    bool finishRecording(qint64 sessionId, qint64 endTimeMs, int totalFrames);

    bool addMarker(qint64 sessionId, quint32 frameIndex, const QString& name);

    // 捕获到未知控件：origin=capture 占位。热路径不要调。
    std::optional<qint64> ensureCaptureKey(const QString& keyId,
                                           const QString& kind,
                                           const QString& valueKind,
                                           const QString& label,
                                           std::optional<double> rangeMin = std::nullopt,
                                           std::optional<double> rangeMax = std::nullopt,
                                           std::optional<int> nativeVk = std::nullopt,
                                           std::optional<int> nativeUsagePage = std::nullopt,
                                           std::optional<int> nativeUsage = std::nullopt);

    static QByteArray packDigitalBlob(int keyCount, const std::vector<int>& pressedIndices);
    static QByteArray packAnalogBlob(const std::vector<float>& normalizedValues);
    static QByteArray packFrameBlob(const QByteArray& digital, const QByteArray& analog);

    // 一次事务写入多帧。WndProc 不要调。
    bool appendFrames(qint64 sessionId, const std::vector<FrameRow>& frames);

private:
    bool execSql(const QString& sql);
    QSqlDatabase db() const;
    bool connectToFile(const QString& dbFilePath);
    int readSchemaVersion() const;
    bool resetSchema();
    bool migrate();
    bool seedBuiltins();

    QString filePath_;
    QString connectionName_;
};
