#include "InputEventBus.h"

#include <spdlog/spdlog.h>

namespace {

const char* overflowName(QueueOverflow overflow) {
    switch (overflow) {
    case QueueOverflow::Block:
        return "block";
    case QueueOverflow::DropOldest:
        return "drop_oldest";
    case QueueOverflow::DropNewest:
        return "drop_newest";
    }
    return "unknown";
}

} // namespace

std::shared_ptr<BoundedQueue<InputEvent>>
InputEventBus::subscribe(EventSubscribeOptions options) {
    spdlog::info("[bus] subscribe name={} capacity={} overflow={} mouseMove={}",
                 options.name, options.capacity, overflowName(options.overflow),
                 options.acceptMouseMove);
    auto queue = std::make_shared<BoundedQueue<InputEvent>>(options.capacity,
                                                            options.overflow);
    std::lock_guard lock(mutex_);
    subscriptions_.push_back(Subscription{std::move(options), queue});
    return queue;
}

void InputEventBus::publish(const InputEvent& event) {
    std::vector<Subscription> snapshot;
    {
        std::lock_guard lock(mutex_);
        snapshot = subscriptions_;
    }
    for (auto& sub : snapshot) {
        if (!sub.options.acceptMouseMove && event.type == InputEventType::MouseMove) {
            continue;
        }
        if (sub.options.overflow == QueueOverflow::Block) {
            if (!sub.queue->push(event)) {
                spdlog::warn("[bus] recorder push failed name={}", sub.options.name);
            }
        } else {
            sub.queue->tryPush(event);
        }
    }
}

void InputEventBus::close() {
    std::lock_guard lock(mutex_);
    spdlog::info("[bus] close subscriptions={}", subscriptions_.size());
    for (auto& sub : subscriptions_) {
        const auto dropped = sub.queue->dropped();
        const auto pending = sub.queue->size();
        sub.queue->close();
        if (dropped > 0) {
            spdlog::warn("[bus] queue '{}' dropped={} pending={}", sub.options.name,
                         dropped, pending);
        } else {
            spdlog::info("[bus] queue '{}' dropped=0 pending={}", sub.options.name,
                         pending);
        }
    }
}
