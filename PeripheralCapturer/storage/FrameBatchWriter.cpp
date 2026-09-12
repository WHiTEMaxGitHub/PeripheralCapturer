#include "FrameBatchWriter.h"

#include <spdlog/spdlog.h>

FrameBatchWriter::FrameBatchWriter(Database& database, qint64 sessionId, int digitalBytes,
                                   int analogBytes, int flushEveryFrames)
    : db_(database), sessionId_(sessionId), digitalBytes_(digitalBytes), analogBytes_(analogBytes),
      flushEvery_(flushEveryFrames < 1 ? 1 : flushEveryFrames) {}

bool FrameBatchWriter::pushFrame(int frame, const QByteArray& blob) {
    const int expect = digitalBytes_ + analogBytes_;
    if (frame < 0 || blob.size() != expect) {
        spdlog::warn("[db] pushFrame bad frame={} size={} expect={}", frame, blob.size(), expect);
        return false;
    }
    pending_.push_back(FrameRow{frame, blob});
    if (static_cast<int>(pending_.size()) >= flushEvery_) {
        return flush();
    }
    return true;
}

bool FrameBatchWriter::flush() {
    if (pending_.empty()) {
        return true;
    }
    if (!db_.appendFrames(sessionId_, pending_)) {
        return false;
    }
    pending_.clear();
    return true;
}

bool FrameBatchWriter::finish() {
    return flush();
}
