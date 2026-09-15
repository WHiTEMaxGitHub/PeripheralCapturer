#include "FrameBatchWriter.h"

#include <spdlog/spdlog.h>

FrameBatchWriter::FrameBatchWriter(Database& database, qint64 sessionId, int digitalBytes,
                                   int analogBytes)
    : db_(database), sessionId_(sessionId), digitalBytes_(digitalBytes), analogBytes_(analogBytes) {}

bool FrameBatchWriter::pushFrame(int frame, const QByteArray& blob) {
    const int expect = digitalBytes_ + analogBytes_;
    if (frame < 0 || blob.size() != expect) {
        spdlog::warn("[db] pushFrame bad frame={} size={} expect={}", frame, blob.size(), expect);
        return false;
    }
    pending_.push_back(FrameRow{frame, blob});
    pendingBytes_ += static_cast<std::size_t>(blob.size());
    if (shouldFlush()) {
        return flush();
    }
    return true;
}

bool FrameBatchWriter::shouldFlush() const {
    return pendingBytes_ >= kFlushBytes ||
           static_cast<int>(pending_.size()) >= kMaxPendingFrames;
}

bool FrameBatchWriter::flush() {
    if (pending_.empty()) {
        return true;
    }
    if (!db_.appendFrames(sessionId_, pending_)) {
        return false;
    }
    pending_.clear();
    pendingBytes_ = 0;
    return true;
}

bool FrameBatchWriter::finish() {
    return flush();
}
