#pragma once

#include "Database.h"

#include <QByteArray>
#include <vector>

// 一行一帧推进内存，满 flushEvery 条再一次事务写入。不合并相同帧。
class FrameBatchWriter {
public:
    static constexpr int kDefaultFlushFrames = 32;

    FrameBatchWriter(Database& database, qint64 sessionId, int digitalBytes, int analogBytes,
                     int flushEveryFrames = kDefaultFlushFrames);

    bool pushFrame(int frame, const QByteArray& blob);
    bool flush();
    bool finish();

    int pending() const { return static_cast<int>(pending_.size()); }

private:
    Database& db_;
    qint64 sessionId_ = 0;
    int digitalBytes_ = 0;
    int analogBytes_ = 0;
    int flushEvery_ = kDefaultFlushFrames;
    std::vector<FrameRow> pending_;
};
