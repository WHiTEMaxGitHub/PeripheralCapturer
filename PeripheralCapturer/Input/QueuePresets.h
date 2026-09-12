#pragma once

#include "InputEventBus.h"
#include "RawInputPacket.h"

#include <cstddef>

// 包队列长度。创建时策略必须是 DropOldest，不要 Block：
//   BoundedQueue<RawInputPacket> packets(kRawPacketQueueCapacity, QueueOverflow::DropOldest);
inline constexpr std::size_t kRawPacketQueueCapacity = 4096;

// 录制订阅：可阻塞、收全量（含 MouseMove），这里不能丢 KeyDown。
inline EventSubscribeOptions recorderSubscribeOptions() {
    EventSubscribeOptions o;
    o.name = "recorder";
    o.capacity = 8192;
    o.overflow = QueueOverflow::Block;
    o.acceptMouseMove = true;
    return o;
}

// 浮层聚合：短队列、丢旧的、不收 MouseMove（位移会把按键挤掉）。
inline EventSubscribeOptions overlaySubscribeOptions() {
    EventSubscribeOptions o;
    o.name = "overlay";
    o.capacity = 64;
    o.overflow = QueueOverflow::DropOldest;
    o.acceptMouseMove = false;
    return o;
}

// 码本「按一下绑定」：更短、临时听、不要位移。
inline EventSubscribeOptions bindListenSubscribeOptions() {
    EventSubscribeOptions o;
    o.name = "bind";
    o.capacity = 32;
    o.overflow = QueueOverflow::DropOldest;
    o.acceptMouseMove = false;
    return o;
}
