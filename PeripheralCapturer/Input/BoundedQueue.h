#pragma once

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>
#include <utility>

// 队列满时的策略，构造时定死。详见 docs/InputQueue.md。
enum class QueueOverflow {
    Block,       // 生产者等到有空位。只给录制事件队列；WndProc 禁用。
    DropOldest,  // 丢掉队头（最旧）再写入。包队列、浮层、绑键用。
    DropNewest,  // 拒绝本条。几乎不用。
};

// 有界、线程安全队列。T 一般为 RawInputPacket 或 InputEvent。
// mutex 不可移动：做成成员或 shared_ptr，不要按值传递。
template <typename T>
class BoundedQueue {
public:
    // capacity==0 视为 1。overflow 之后不变。
    explicit BoundedQueue(std::size_t capacity, QueueOverflow overflow)
        : capacity_(capacity == 0 ? 1 : capacity), overflow_(overflow) {}

    // 置 closed_，叫醒所有 wait 在 pop/push 上的线程。之后不能再入队；剩余元素仍可取出。
    void close() {
        std::lock_guard lock(mutex_);
        closed_ = true;
        notEmpty_.notify_all();
        notFull_.notify_all();
    }

    bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

    // 当前条数。热路径不必每包查询。
    std::size_t size() const {
        std::lock_guard lock(mutex_);
        return items_.size();
    }

    // 因满员丢弃的累计次数（DropOldest / DropNewest）。Block 下应为 0。
    std::size_t dropped() const {
        std::lock_guard lock(mutex_);
        return dropped_;
    }

    // 非阻塞入队。WndProc 必须走这里。Block 且已满时返回 false，不会在消息循环里睡。
    bool tryPush(T item) { return pushImpl(std::move(item), false); }

    // 可阻塞入队。仅处理线程往录制队列送时用。
    bool push(T item) { return pushImpl(std::move(item), true); }

    // 非阻塞出队。空则 nullopt。浮层可「有就取」。
    std::optional<T> tryPop() {
        std::lock_guard lock(mutex_);
        if (items_.empty()) {
            return std::nullopt;
        }
        T out = std::move(items_.front());
        items_.pop_front();
        notFull_.notify_one();
        return out;
    }

    // 空则等待，直到有数据或 close。关闭且取尽返回 nullopt，作为消费者循环结束条件。
    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        notEmpty_.wait(lock, [this] { return closed_ || !items_.empty(); });
        if (items_.empty()) {
            return std::nullopt;
        }
        T out = std::move(items_.front());
        items_.pop_front();
        notFull_.notify_one();
        return out;
    }

private:
    // waitWhenFull==false 对应 tryPush；true 对应 push。
    bool pushImpl(T&& item, bool waitWhenFull) {
        std::unique_lock lock(mutex_);
        if (closed_) {
            return false;
        }
        if (items_.size() >= capacity_) {
            switch (overflow_) {
            case QueueOverflow::Block:
                if (!waitWhenFull) {
                    return false;
                }
                notFull_.wait(lock, [this] { return closed_ || items_.size() < capacity_; });
                if (closed_ || items_.size() >= capacity_) {
                    return false;
                }
                break;
            case QueueOverflow::DropOldest:
                items_.pop_front();
                ++dropped_;
                break;
            case QueueOverflow::DropNewest:
                ++dropped_;
                return false;
            }
        }
        items_.push_back(std::move(item));
        notEmpty_.notify_one();
        return true;
    }

    const std::size_t capacity_;   // 最多存放条数，防止 1000Hz 吃光内存
    const QueueOverflow overflow_; // 满员策略
    mutable std::mutex mutex_;     // 保护 items_/closed_/dropped_；mutable 以便 const 查询加锁
    std::condition_variable notEmpty_; // 从空→非空时叫醒 pop
    std::condition_variable notFull_;  // 从满→有空位时叫醒 Block 的 push；不可与 notEmpty_ 合成一个
    std::deque<T> items_;          // 队头出/丢最旧，队尾入新
    bool closed_ = false;          // close() 后入队失败
    std::size_t dropped_ = 0;      // 满员丢弃计数，热路径不打日志
};
