#pragma once

#include "BoundedQueue.h"
#include "DeviceRegistry.h"
#include "InputEventBus.h"
#include "QueuePresets.h"
#include "RawInputPacket.h"

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

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

    void setOnToggleRecord(std::function<void()> cb) { onToggleRecord_ = std::move(cb); }
    void setOnTogglePovClick(std::function<void()> cb) { onTogglePovClick_ = std::move(cb); }

    // UI 线程从 key_codes 拷一份；(kind, native_vk) → key_id。处理线程禁止碰 SQLite。
    struct NativeVkBinding {
        std::string kind;
        int nativeVk = 0;
        std::string keyId;
    };
    void reloadNativeVkMap(std::vector<NativeVkBinding> bindings);

private:
    std::string lookupNativeControl(std::string_view kind, int nativeVk,
                                    const std::string& fallback) const;
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
    std::function<void()> onToggleRecord_;
    std::function<void()> onTogglePovClick_;

    mutable std::mutex vkMapMutex_;
    std::unordered_map<std::string, std::string> nativeVkMap_;
};
