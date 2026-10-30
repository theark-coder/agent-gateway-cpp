
#pragma once

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <queue>
#include <stdexcept>
#include <utility>

namespace agent {

template <typename T>
class BoundedQueue {
public:
    // 构造函数
    explicit BoundedQueue(std::size_t capacity)
        : capacity_(capacity) {

        if (capacity == 0) {
            throw std::invalid_argument(
                "capacity must be greater than zero"
            );
        }
    }

    // 复制添加
    bool try_push(const T& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (closed_ || queue_.size() >= capacity_) {
                return false;
            }

            queue_.push(value);
        }

        not_empty_.notify_one();
        return true;
    }

    // 移动添加
    bool try_push(T&& value) {
        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (closed_ || queue_.size() >= capacity_) {
                return false;
            }

            queue_.push(std::move(value));
        }

        not_empty_.notify_one();
        return true;
    }

    // 获取任务
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);

        not_empty_.wait(lock, [this] {
            return closed_ || !queue_.empty();
        });

        if (queue_.empty()) {
            return std::nullopt;
        }

        T value = std::move(queue_.front());
        queue_.pop();

        return value;
    }

    // 关闭队列
    void close() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            closed_ = true;
        }

        not_empty_.notify_all();
    }

private:
    const std::size_t capacity_;

    std::queue<T> queue_;

    std::mutex mutex_;

    std::condition_variable not_empty_;

    bool closed_ = false;
};

} // namespace agent
