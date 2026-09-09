#include "InputEventBus.h"

std::shared_ptr<BoundedQueue<InputEvent>>
InputEventBus::subscribe(EventSubscribeOptions options) {
    auto queue = std::make_shared<BoundedQueue<InputEvent>>(options.capacity,
                                                            options.overflow);
    std::lock_guard lock(mutex_);
    subscriptions_.push_back(Subscription{std::move(options), queue});
    return queue;
}

void InputEventBus::publish(const InputEvent& event) {
    // 先拷订阅表再入队：录制 Block 时 push 可能长时间等待，
    // 若仍握着 mutex_，其它线程无法 subscribe，本条也送不进后续订阅者。
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
            sub.queue->push(event);
        } else {
            sub.queue->tryPush(event);
        }
    }
}

void InputEventBus::close() {
    std::lock_guard lock(mutex_);
    for (auto& sub : subscriptions_) {
        sub.queue->close();
    }
}
