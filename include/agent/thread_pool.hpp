
#pragma once

#include "agent/bounded_queue.hpp"

#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace agent {

class ThreadPool final {
public:
    explicit ThreadPool(
        std::size_t thread_count,
        std::size_t queue_capacity = 1024
    );

    ~ThreadPool();
    void shutdown();
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args)
        -> std::future<std::invoke_result_t<F, Args...>> {

        using ReturnType = std::invoke_result_t<F, Args...>;

        auto bound_task = std::bind(
            std::forward<F>(f),
            std::forward<Args>(args)...
        );

        auto task =
            std::make_shared<std::packaged_task<ReturnType()>>(
                std::move(bound_task)
            );

        auto result = task->get_future();

        std::function<void()> wrapper = [task]() {
            (*task)();
        };

        {
            std::lock_guard<std::mutex> lock(state_mutex_);

            if (stopping_) {
                throw std::runtime_error(
                    "submit on stopped ThreadPool"
                );
            }

            if (!tasks_.try_push(std::move(wrapper))) {
                throw std::runtime_error(
                    "thread pool task queue is full"
                );
            }
        }

        return result;
    }

private:
    void worker_loop();

    std::vector<std::thread> workers_;

    BoundedQueue<std::function<void()>> tasks_;

    std::mutex state_mutex_;
    std::mutex shutdown_mutex_;
    bool stopping_ = false;
};

} // namespace agent
