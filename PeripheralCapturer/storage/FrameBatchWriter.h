#pragma once

#include "Database.h"

#include <QByteArray>
#include <cstddef>
#include <vector>

// 一行一帧推进内存，满体积或满帧数再一次事务写入。不合并相同帧。
class FrameBatchWriter {
public:
    // 当前键盘+鼠标一帧大约 20 字节：1MB 大约十几分钟。体积上限给以后更大的 blob；
    // 现在会先撞帧数上限（60fps 大约半分钟）。
    static constexpr std::size_t kFlushBytes = 1024 * 1024;
    static constexpr int kMaxPendingFrames = 2048;

    FrameBatchWriter(Database& database, qint64 sessionId, int digitalBytes, int analogBytes);

    bool pushFrame(int frame, const QByteArray& blob);
    bool flush();
    bool finish();

    int pending() const { return static_cast<int>(pending_.size()); }

private:
    bool shouldFlush() const;

    Database& db_;
    qint64 sessionId_ = 0;
    int digitalBytes_ = 0;
    int analogBytes_ = 0;
    std::size_t pendingBytes_ = 0;
    std::vector<FrameRow> pending_;
};
