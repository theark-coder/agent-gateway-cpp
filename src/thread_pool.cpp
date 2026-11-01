
#include "agent/thread_pool.hpp"

#include <stdexcept>
#include <utility>

namespace agent {

ThreadPool::ThreadPool(
    std::size_t thread_count,
    std::size_t queue_capacity
)
    : tasks_(queue_capacity) {

    if (thread_count == 0) {
        throw std::invalid_argument(
            "thread_count must be greater than zero"
        );
    }

    workers_.reserve(thread_count);

    try {
        for (std::size_t i = 0; i < thread_count; ++i) {
            workers_.emplace_back(
                &ThreadPool::worker_loop,
                this
            );
        }
    } catch (...) {
        tasks_.close();

        for (auto& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }

        throw;
    }
}

void ThreadPool::enqueue(std::function<void()> task) {
    std::lock_guard<std::mutex> lock(state_mutex_);

    if (stopping_) {
        throw std::runtime_error(
            "submit on stopped ThreadPool"
        );
    }

    if (!tasks_.try_push(std::move(task))) {
        throw std::runtime_error(
            "thread pool task queue is full"
        );
    }

    // 只有成功进入队列的任务才计入 submitted
    submitted_.fetch_add(1, std::memory_order_relaxed);
}

void ThreadPool::mark_completed() {
    std::lock_guard<std::mutex> lock(state_mutex_);

    completed_.fetch_add(1, std::memory_order_relaxed);
}

ThreadPoolStats ThreadPool::stats() const {
    std::lock_guard<std::mutex> lock(state_mutex_);

    return {
        submitted_.load(std::memory_order_relaxed),
        completed_.load(std::memory_order_relaxed)
    };
}

void ThreadPool::shutdown() {
    std::lock_guard<std::mutex> shutdown_lock(
        shutdown_mutex_
    );

    {
        std::lock_guard<std::mutex> lock(state_mutex_);

        if (!stopping_) {
            stopping_ = true;
            tasks_.close();
        }
    }

    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

void ThreadPool::worker_loop() {
    while (true) {
        auto task = tasks_.pop();

        if (!task.has_value()) {
            return;
        }

        (*task)();
    }
}

} // namespace agent
