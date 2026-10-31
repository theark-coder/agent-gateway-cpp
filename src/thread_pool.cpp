
#include "agent/thread_pool.hpp"

#include <stdexcept>

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

void ThreadPool::shutdown() {
    // 保证多个调用者不会同时 join 同一批线程
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

    // 等待所有已接纳的任务执行完毕
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
