#pragma once

#include "BoundedQueue.h"
#include "InputEvent.h"

#include <memory>
#include <mutex>
#include <string>
#include <vector>

// 一条订阅 = 处理线程扇出的一条独立 BoundedQueue<InputEvent>。
// 完整架构与误用见 docs/InputQueue.md。
struct EventSubscribeOptions {
    std::string name; // 仅调试展示（recorder / overlay / bind），逻辑不解析此字符串
    std::size_t capacity = 4096; // 该订阅者专用队列长度
    QueueOverflow overflow = QueueOverflow::DropOldest; // 录制必须改为 Block
    // false：publish 时跳过 MouseMove，事件不进本队列（浮层/绑键）
    bool acceptMouseMove = true;
};

// 进程内 InputEvent 扇出。不是网络、不是 Qt 信号、不搬运 RawInputPacket。
// DeviceRegistry / 路由在 publish 之前，不要登记为订阅者。
class InputEventBus {
public:
    // 按选项建队列，登记到表，把同一个 shared_ptr 还给调用方。
    // 调用方只 pop；总线负责 push。
    std::shared_ptr<BoundedQueue<InputEvent>> subscribe(EventSubscribeOptions options);

    // 处理线程在确认「状态变化」后调用。按订阅过滤后拷贝入各队列。
    void publish(const InputEvent& event);

    // 关闭所有订阅队列，消费者 pop 循环可退出。
    void close();

private:
    struct Subscription {
        EventSubscribeOptions options; // 仍用于 MouseMove 过滤和选择 push/tryPush
        std::shared_ptr<BoundedQueue<InputEvent>> queue; // 与订阅者共享
    };

    std::mutex mutex_; // 只保护 subscriptions_（谁在听），不保护各队列内部
    std::vector<Subscription> subscriptions_;
};
