
#pragma once

#include "agent/bounded_queue.hpp"

#include <atomic>
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

class ThreadPool;

// 任务取消令牌：工作线程读取
class CancellationToken final {
public:
    bool stop_requested() const noexcept {
        return flag_->load(std::memory_order_relaxed);
    }

private:
    explicit CancellationToken(
        std::shared_ptr<std::atomic<bool>> flag
    ) : flag_(std::move(flag)) {}

    std::shared_ptr<std::atomic<bool>> flag_;

    friend class ThreadPool;
};

// 任务取消句柄：调用者请求取消
class CancellationHandle final {
public:
    void request_cancel() const noexcept {
        flag_->store(true, std::memory_order_relaxed);
    }

    bool stop_requested() const noexcept {
        return flag_->load(std::memory_order_relaxed);
    }

private:
    explicit CancellationHandle(
        std::shared_ptr<std::atomic<bool>> flag
    ) : flag_(std::move(flag)) {}

    std::shared_ptr<std::atomic<bool>> flag_;

    friend class ThreadPool;
};

// 支持取消的任务返回对象
template <typename T>
struct CancellableTask {
    std::future<T> future;
    CancellationHandle cancellation;
};

// 线程池统计信息
struct ThreadPoolStats {
    std::size_t submitted = 0;
    std::size_t completed = 0;
};

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

    // 原有普通任务提交接口
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

        enqueue([this, task]() {
            (*task)();
            mark_completed();
        });

        return result;
    }

    // 支持协作式取消的任务提交接口
    // 被提交的函数第一个参数必须是 CancellationToken
    template <typename F, typename... Args>
    auto submit_cancellable(F&& f, Args&&... args)
        -> CancellableTask<
            std::invoke_result_t<F, CancellationToken, Args...>
        > {

        using ReturnType =
            std::invoke_result_t<F, CancellationToken, Args...>;

        auto flag = std::make_shared<std::atomic<bool>>(false);

        CancellationToken token(flag);
        CancellationHandle cancellation(flag);

        auto bound_task = std::bind(
            std::forward<F>(f),
            token,
            std::forward<Args>(args)...
        );

        auto task =
            std::make_shared<std::packaged_task<ReturnType()>>(
                std::move(bound_task)
            );

        auto result = task->get_future();

        enqueue([this, task]() {
            (*task)();
            mark_completed();
        });

        return {
            std::move(result),
            std::move(cancellation)
        };
    }

    // 获取线程池统计信息
    ThreadPoolStats stats() const;

private:
    void worker_loop();

    void enqueue(std::function<void()> task);

    void mark_completed();

    std::vector<std::thread> workers_;

    BoundedQueue<std::function<void()>> tasks_;

    mutable std::mutex state_mutex_;
    std::mutex shutdown_mutex_;

    bool stopping_ = false;

    std::atomic<std::size_t> submitted_{0};
    std::atomic<std::size_t> completed_{0};
};

} // namespace agent
