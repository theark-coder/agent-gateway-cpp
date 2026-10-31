
#include "agent/thread_pool.hpp"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

// 测试1：4个工作线程执行100个任务
bool test_100_tasks_and_thread_limit() {
    agent::ThreadPool pool(4, 128);

    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::atomic<int> completed{0};

    std::vector<std::future<int>> futures;
    futures.reserve(100);

    for (int i = 0; i < 100; ++i) {
        futures.emplace_back(pool.submit([&, i] {
            int current = active.fetch_add(1) + 1;

            int old = peak.load();

            while (current > old &&
                   !peak.compare_exchange_weak(old, current)) {
            }

            std::this_thread::sleep_for(
                std::chrono::milliseconds(2)
            );

            completed.fetch_add(1);
            active.fetch_sub(1);

            return i * i;
        }));
    }

    bool results_ok = true;

    for (int i = 0; i < 100; ++i) {
        if (futures[i].get() != i * i) {
            results_ok = false;
        }
    }

    pool.shutdown();

    return results_ok &&
           completed.load() == 100 &&
           peak.load() <= 4;
}

// 测试2：Future异常传播
bool test_future_exception() {
    agent::ThreadPool pool(1, 4);

    auto future = pool.submit([]() -> int {
        throw std::runtime_error("task failure");
    });

    bool caught = false;

    try {
        future.get();
    } catch (const std::runtime_error&) {
        caught = true;
    }

    pool.shutdown();

    return caught;
}

// 测试3：队列满时拒绝新任务
bool test_queue_full_rejection() {
    agent::ThreadPool pool(1, 1);

    std::promise<void> started_promise;
    auto started = started_promise.get_future();

    std::promise<void> release_promise;
    auto release = release_promise.get_future().share();

    // 第一个任务阻塞工作线程
    auto first = pool.submit([&started_promise, release] {
        started_promise.set_value();
        release.wait();
        return 1;
    });

    // 确保第一个任务已经开始执行
    started.wait();

    // 第二个任务占满队列
    auto second = pool.submit([] {
        return 2;
    });

    // 第三个任务应该被拒绝
    bool rejected = false;

    try {
        auto third = pool.submit([] {
            return 3;
        });
        (void)third;
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    // 释放第一个任务
    release_promise.set_value();

    bool results_ok =
        first.get() == 1 &&
        second.get() == 2;

    pool.shutdown();

    return rejected && results_ok;
}

// 测试4：shutdown排空任务并拒绝新任务
bool test_shutdown_drain_and_rejection() {
    agent::ThreadPool pool(2, 16);

    std::atomic<int> completed{0};

    std::vector<std::future<int>> futures;

    for (int i = 0; i < 10; ++i) {
        futures.emplace_back(pool.submit([&, i] {
            std::this_thread::sleep_for(
                std::chrono::milliseconds(5)
            );

            completed.fetch_add(1);
            return i;
        }));
    }

    // 关闭线程池，等待已接纳任务执行完成
    pool.shutdown();

    bool results_ok = true;

    for (int i = 0; i < 10; ++i) {
        if (futures[i].get() != i) {
            results_ok = false;
        }
    }

    // 关闭后拒绝新任务
    bool rejected = false;

    try {
        auto future = pool.submit([] {
            return 100;
        });
        (void)future;
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    // 验证重复关闭安全
    pool.shutdown();

    return results_ok &&
           completed.load() == 10 &&
           rejected;
}

int main() {
    if (!test_100_tasks_and_thread_limit()) {
        std::cerr << "FAIL: 100 tasks and thread limit\n";
        return 1;
    }

    std::cout << "100 tasks and thread limit passed\n";

    if (!test_future_exception()) {
        std::cerr << "FAIL: future exception\n";
        return 1;
    }

    std::cout << "future exception passed\n";

    if (!test_queue_full_rejection()) {
        std::cerr << "FAIL: queue full rejection\n";
        return 1;
    }

    std::cout << "queue full rejection passed\n";

    if (!test_shutdown_drain_and_rejection()) {
        std::cerr << "FAIL: shutdown drain and rejection\n";
        return 1;
    }

    std::cout << "shutdown drain and rejection passed\n";

    std::cout << "All Day11 ThreadPool tests passed\n";

    return 0;
}
