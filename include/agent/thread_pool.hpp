
#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <utility>
#include <vector>

namespace agent {

class ThreadPool final {
public:
    explicit ThreadPool(std::size_t thread_count);

    ~ThreadPool();

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

        std::future<ReturnType> result = task->get_future();

        {
            std::lock_guard<std::mutex> lock(mutex_);

            if (stopping_) {
                throw std::runtime_error(
                    "submit on stopped ThreadPool"
                );
            }

            tasks_.emplace([task]() {
                (*task)();
            });
        }

        cv_.notify_one();

        return result;
    }

private:
    void worker_loop();

    std::vector<std::thread> workers_;

    std::queue<std::function<void()>> tasks_;

    std::mutex mutex_;

    std::condition_variable cv_;

    bool stopping_ = false;
};

}  // namespace agent
