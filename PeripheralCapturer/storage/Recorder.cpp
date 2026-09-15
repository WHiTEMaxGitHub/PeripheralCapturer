#include "Recorder.h"

#include "AppConfig.h"
#include "Database.h"
#include "FrameBatchWriter.h"
#include "RecordingLayout.h"
#include "../Input/InputEvent.h"
#include "../Input/QueuePresets.h"
#include "../Input/Timer.h"

#include <QDateTime>

#include <algorithm>
#include <chrono>
#include <spdlog/spdlog.h>
#include <utility>

namespace {
constexpr auto kRecConnection = "pc-rec";
}

Recorder::Recorder(InputEventBus& bus, QString dbPath) : dbPath_(std::move(dbPath)) {
    auto opts = recorderSubscribeOptions();
    queue_ = bus.subscribe(std::move(opts));
    running_.store(true, std::memory_order_relaxed);
    worker_ = std::thread([this] { run(); });
}

Recorder::~Recorder() {
    stop();
}

void Recorder::stop() {
    running_.store(false, std::memory_order_relaxed);
    if (worker_.joinable()) {
        worker_.join();
    }
}

void Recorder::toggle() {
    pending_.store(Cmd::Toggle, std::memory_order_release);
}

void Recorder::requestStop() {
    pending_.store(Cmd::Stop, std::memory_order_release);
}

void Recorder::prepareForDbWipe() {
    requestStop();
    for (int i = 0; i < 40 && recording_.load(std::memory_order_relaxed); ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
}

void Recorder::run() {
    // addDatabase / 查询必须在本线程。不要复用 UI 的 "pc"。
    Database recDb{QString::fromLatin1(kRecConnection)};
    spdlog::info("[db] recorder thread ready");
    while (running_.load(std::memory_order_relaxed)) {
        applyCommand(recDb);
        if (auto event = queue_->popFor(std::chrono::milliseconds(50))) {
            if (recording_.load(std::memory_order_relaxed)) {
                ingest(*event, recDb);
            }
        }
    }
    if (recording_.load(std::memory_order_relaxed)) {
        endSession(recDb);
    }
    if (recDb.isOpen()) {
        recDb.close();
    }
}

void Recorder::applyCommand(Database& recDb) {
    const Cmd cmd = pending_.exchange(Cmd::None, std::memory_order_acq_rel);
    if (cmd == Cmd::None) {
        return;
    }
    const bool on = recording_.load(std::memory_order_relaxed);
    if (cmd == Cmd::Stop || (cmd == Cmd::Toggle && on)) {
        if (on) {
            endSession(recDb);
        }
        return;
    }
    if (cmd == Cmd::Toggle && !on) {
        beginSession(recDb);
    }
}

bool Recorder::beginSession(Database& recDb) {
    const AppConfig cfg = AppConfig::load();
    BeginRecordingRequest req;
    req.fps = cfg.recordingFps;
    req.deviceBits = cfg.recordingDeviceBits();
    if (req.deviceBits == 0) {
        spdlog::error("[db] recorder start refused: device_bits=0");
        return false;
    }
    if (!recDb.isOpen() && !recDb.open(dbPath_)) {
        spdlog::error("[db] recorder open failed path={}", dbPath_.toStdString());
        return false;
    }
    Timer::setSessionFps(req.fps);
    const auto session = recDb.beginRecording(req);
    if (!session) {
        recDb.close();
        return false;
    }
    bits_ = session->layout.deviceBits;
    fps_ = session->fps;
    digitalCount_ = session->layout.digitalCount;
    analogCount_ = session->layout.analogCount;
    digital_.assign(static_cast<size_t>(digitalCount_), 0);
    analog_.assign(static_cast<size_t>(analogCount_), 0.f);
    const int digitalBytes = digitalCount_ <= 0 ? 0 : (digitalCount_ + 7) / 8;
    const int analogBytes = analogCount_ * static_cast<int>(sizeof(float));
    writer_ = std::make_unique<FrameBatchWriter>(recDb, session->sessionId, digitalBytes,
                                                 analogBytes);
    // 本场帧号从开录时刻起算。event.frameIndex 是进程寿命，不能当 sessions.frame。
    originUs_ = Timer::nowUs();
    currentFrame_ = 0;
    haveFrame_ = false;
    writtenFrames_ = 0;
    sessionId_.store(session->sessionId, std::memory_order_relaxed);
    recording_.store(true, std::memory_order_relaxed);
    spdlog::info("[db] recorder ingesting id={} fps={} bits={:#x}", session->sessionId, fps_, bits_);
    return true;
}

void Recorder::endSession(Database& recDb) {
    if (!recording_.load(std::memory_order_relaxed)) {
        return;
    }
    const int endFrame = std::max(0, sessionFrame(Timer::nowUs()));
    if (!haveFrame_) {
        haveFrame_ = true;
        currentFrame_ = 0;
    }
    advanceTo(endFrame, recDb);
    writeFrame(endFrame, recDb);
    if (writer_) {
        writer_->finish();
        writer_.reset();
    }
    const qint64 id = sessionId_.load(std::memory_order_relaxed);
    recDb.finishRecording(id, QDateTime::currentMSecsSinceEpoch(), writtenFrames_);
    haveFrame_ = false;
    recDb.close();
    sessionId_.store(0, std::memory_order_relaxed);
    // 先关 pc-rec 再清 recording_，Debug 删库等的是这个标志。
    recording_.store(false, std::memory_order_relaxed);
}

void Recorder::ingest(const InputEvent& event, Database& recDb) {
    const int frame = sessionFrame(event.timestampUs);
    if (frame < 0) {
        return;
    }
    if (!haveFrame_) {
        haveFrame_ = true;
        currentFrame_ = 0;
    }
    advanceTo(frame, recDb);
    applyEvent(event);
}

void Recorder::applyEvent(const InputEvent& event) {
    switch (event.type) {
    case InputEventType::KeyDown:
    case InputEventType::MouseButtonDown:
    case InputEventType::ButtonDown: {
        if (const auto idx = RecLayout::digitalIndex(bits_, event.control)) {
            if (*idx >= 0 && *idx < digitalCount_) {
                digital_[static_cast<size_t>(*idx)] = 1;
            }
        }
        break;
    }
    case InputEventType::KeyUp:
    case InputEventType::MouseButtonUp:
    case InputEventType::ButtonUp: {
        if (const auto idx = RecLayout::digitalIndex(bits_, event.control)) {
            if (*idx >= 0 && *idx < digitalCount_) {
                digital_[static_cast<size_t>(*idx)] = 0;
            }
        }
        break;
    }
    case InputEventType::AxisChanged: {
        if (const auto idx = RecLayout::analogIndex(bits_, event.control)) {
            if (*idx >= 0 && *idx < analogCount_) {
                analog_[static_cast<size_t>(*idx)] = event.normalizedValue;
            }
        }
        break;
    }
    default:
        break;
    }
}

void Recorder::advanceTo(int frame, Database& recDb) {
    while (currentFrame_ < frame) {
        writeFrame(currentFrame_, recDb);
        ++currentFrame_;
    }
}

bool Recorder::writeFrame(int frame, Database& recDb) {
    (void)recDb;
    if (!writer_) {
        return false;
    }
    if (!writer_->pushFrame(frame, packCurrent())) {
        return false;
    }
    ++writtenFrames_;
    return true;
}

QByteArray Recorder::packCurrent() const {
    std::vector<int> pressed;
    pressed.reserve(static_cast<size_t>(digitalCount_));
    for (int i = 0; i < digitalCount_; ++i) {
        if (digital_[static_cast<size_t>(i)]) {
            pressed.push_back(i);
        }
    }
    return Database::packFrameBlob(Database::packDigitalBlob(digitalCount_, pressed),
                                   Database::packAnalogBlob(analog_));
}

int Recorder::sessionFrame(int64_t timestampUs) const {
    const int64_t rel = timestampUs - originUs_;
    if (rel < 0) {
        return -1;
    }
    return static_cast<int>(Timer::ToFrameIndex(rel, fps_));
}
