#pragma once

#include "../Input/InputEventBus.h"

#include <QString>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

class Database;
class FrameBatchWriter;

// 常驻消费 recorder 队列；只在本线程碰 SQLite。空闲丢事件，开录才归并写盘。
class Recorder {
public:
    Recorder(InputEventBus& bus, QString dbPath);
    ~Recorder();

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    void toggle();
    void requestStop();
    void stop();

    bool recording() const { return recording_.load(std::memory_order_relaxed); }
    qint64 sessionId() const { return sessionId_.load(std::memory_order_relaxed); }

    // Debug 删库前：停录并关掉 pc-rec，避免 WAL 锁文件。
    void prepareForDbWipe();

private:
    enum class Cmd : int { None, Toggle, Stop };

    void run();
    void applyCommand(Database& recDb);
    bool beginSession(Database& recDb);
    void endSession(Database& recDb);
    void ingest(const InputEvent& event, Database& recDb);
    void applyEvent(const InputEvent& event);
    void advanceTo(int frame, Database& recDb);
    bool writeFrame(int frame, Database& recDb);
    QByteArray packCurrent() const;
    int sessionFrame(int64_t timestampUs) const;

    std::shared_ptr<BoundedQueue<InputEvent>> queue_;
    QString dbPath_;
    std::thread worker_;
    std::atomic<bool> running_{false};
    std::atomic<bool> recording_{false};
    std::atomic<qint64> sessionId_{0};
    std::atomic<Cmd> pending_{Cmd::None};

    uint16_t bits_ = 0;
    int fps_ = 60;
    int digitalCount_ = 0;
    int analogCount_ = 0;
    std::vector<uint8_t> digital_;
    std::vector<float> analog_;
    int64_t originUs_ = 0;
    int currentFrame_ = 0;
    bool haveFrame_ = false;
    int writtenFrames_ = 0;
    std::unique_ptr<FrameBatchWriter> writer_;
};
