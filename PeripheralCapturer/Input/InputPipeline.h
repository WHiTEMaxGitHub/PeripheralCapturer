#pragma once

#include "BoundedQueue.h"
#include "DeviceRegistry.h"
#include "InputEventBus.h"
#include "QueuePresets.h"
#include "RawInputPacket.h"

#include <atomic>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <Xinput.h>

// 处理线程：包队列 → 键鼠差分；同线程超时醒来扫 XInput。不写盘、不订 Recorder。
class InputPipeline {
public:
    InputPipeline()
        : packets_(kRawPacketQueueCapacity, QueueOverflow::DropOldest) {}

    bool start();
    void stop();

    BoundedQueue<RawInputPacket>& packets() { return packets_; }
    InputEventBus& bus() { return bus_; }

    bool running() const { return running_.load(std::memory_order_relaxed); }
    uint64_t publishedCount() const { return published_.load(std::memory_order_relaxed); }
    std::size_t packetDropped() const { return packets_.dropped(); }

private:
    void run();
    void processPacket(const RawInputPacket& pkt);
    void processKeyboard(const RawInputPacket& pkt, const std::string& deviceID);
    void processMouse(const RawInputPacket& pkt, const std::string& deviceID);
    void processRemoval(const RawInputPacket& pkt);
    void pollXInput();
    void publish(InputEvent event);
    void noteFirst(const InputEvent& event);

    BoundedQueue<RawInputPacket> packets_;
    InputEventBus bus_;
    DeviceRegistry registry_;

    struct KeyboardRuntime {
        std::unordered_set<uint32_t> down;
    };
    struct MouseRuntime {
        bool hasAbs = false;
        long lastX = 0;
        long lastY = 0;
    };
    struct XInputRuntime {
        bool connected = false;
        WORD buttons = 0;
        BYTE leftTrigger = 0;
        BYTE rightTrigger = 0;
        SHORT thumbLX = 0;
        SHORT thumbLY = 0;
        SHORT thumbRX = 0;
        SHORT thumbRY = 0;
        float nlt = 0.f;
        float nrt = 0.f;
        float nlx = 0.f;
        float nly = 0.f;
        float nrx = 0.f;
        float nry = 0.f;
    };

    std::unordered_map<HANDLE, KeyboardRuntime> keyboards_;
    std::unordered_map<HANDLE, MouseRuntime> mice_;
    XInputRuntime pads_[4]{};

    std::unordered_set<std::string> seenFirst_;
    std::atomic<uint64_t> published_{0};
    std::size_t lastDropped_ = 0;
    int64_t lastDropLogUs_ = 0;

    std::thread worker_;
    std::atomic<bool> running_{false};
};
