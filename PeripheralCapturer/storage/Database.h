#pragma once

#include <QByteArray>
#include <QString>
#include <optional>
#include <vector>

// SQLite：编码表 + 会话元数据 + 派生帧 BLOB。
// 帧通道对齐示例工程：bitset / float32 进 BLOB，连续相同状态用 run_len 合并，
// 不要每帧一行、更不要每个按键一行。全量 InputEvent 仍在 .bin。
struct BeginRecordingRequest {
    QString displayName;
    int fps = 60;
    QString recordingConfigJson = QStringLiteral("{}");
    QString profileNameSnapshot;
};

struct RecordingSession {
    qint64 sessionId = 0;
    int fps = 60;
    QString eventLogRelative; // 相对应用目录，写入 sessions.event_log_path
    QString eventLogAbsolute;
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
    bool open(const QString& dbFilePath);
    void close();
    bool isOpen() const;

    std::optional<KeyCodeRecord> findByKeyId(const QString& keyId) const;
    std::optional<KeyCodeRecord> findByNativeVk(const QString& kind, int nativeVk) const;
    std::optional<KeyCodeRecord> findByNativeHid(int usagePage, int usage) const;

    // 开录：插入 sessions（end_time NULL），冻结 fps 与 recording JSON，并创建空 log 文件。
    std::optional<RecordingSession> beginRecording(const BeginRecordingRequest& req);

    // 停录：回填时长与计数。不要在热路径里每条事件 UPDATE。
    bool finishRecording(qint64 sessionId, qint64 endTimeMs, qint64 totalEvents, int totalFrames);

    bool addMarker(qint64 sessionId, quint32 frameIndex, const QString& name);

    // 本场第一次见到某码时调用，写入 session_keys / session_axes，返回下标。
    std::optional<int> bindSessionControl(qint64 sessionId, const QString& keyId);

    // 捕获到未知控件：origin=capture 占位，并把原生码写入编码表，已存在则返回原 id。
    std::optional<qint64> ensureCaptureKey(const QString& keyId,
                                           const QString& kind,
                                           const QString& valueKind,
                                           const QString& label,
                                           std::optional<double> rangeMin = std::nullopt,
                                           std::optional<double> rangeMax = std::nullopt,
                                           std::optional<int> nativeVk = std::nullopt,
                                           std::optional<int> nativeUsagePage = std::nullopt,
                                           std::optional<int> nativeUsage = std::nullopt);

    // 小端 bit：第 i 位对应 session_keys.key_index。长度 ceil(keyCount/8)。
    static QByteArray packDigitalBlob(int keyCount, const std::vector<int>& pressedIndices);
    // 按 axis_index 排列的 float32（本机小端，已按码本范围归一化）。
    static QByteArray packAnalogBlob(const std::vector<float>& normalizedValues);

    // 内存里把相同状态攒成一条再调。WndProc / 逐帧热路径不要直接 INSERT。
    bool appendDigitalRun(qint64 sessionId, int startFrame, int runLen, const QByteArray& stateBlob);
    bool appendAnalogRun(qint64 sessionId, int startFrame, int runLen, const QByteArray& valuesBlob);

private:
    bool execSql(const QString& sql);
    bool migrate();
    bool seedBuiltins();
};
