
#include "agent/thread_pool.hpp"

#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

// 测试1：100个任务的原子统计
bool test_atomic_statistics() {
    agent::ThreadPool pool(4, 128);

    std::vector<std::future<int>> futures;

    for (int i = 0; i < 100; ++i) {
        futures.emplace_back(
            pool.submit([i] {
                return i * i;
            })
        );
    }

    bool results_ok = true;

    for (int i = 0; i < 100; ++i) {
        if (futures[i].get() != i * i) {
            results_ok = false;
        }
    }

    // 验证异常结束的任务也计入 completed
    auto failed = pool.submit([]() -> int {
        throw std::runtime_error("expected failure");
    });

    bool exception_caught = false;

    try {
        (void)failed.get();
    } catch (const std::runtime_error&) {
        exception_caught = true;
    }

    pool.shutdown();

    const auto stats = pool.stats();

    return results_ok &&
           exception_caught &&
           stats.submitted == 101 &&
           stats.completed == 101;
}

// 测试2：取消标记独立性
bool test_independent_cancellation() {
    agent::ThreadPool pool(2, 8);

    std::promise<void> first_started;
    std::promise<void> second_started;

    auto first_ready = first_started.get_future();
    auto second_ready = second_started.get_future();

    auto first = pool.submit_cancellable(
        [&first_started](agent::CancellationToken token) {
            first_started.set_value();

            while (!token.stop_requested()) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1)
                );
            }

            return 11;
        }
    );

    auto second = pool.submit_cancellable(
        [&second_started](agent::CancellationToken token) {
            second_started.set_value();

            while (!token.stop_requested()) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(1)
                );
            }

            return 22;
        }
    );

    first_ready.wait();
    second_ready.wait();

    // 只取消第一个任务
    first.cancellation.request_cancel();

    const bool first_ok = first.future.get() == 11;

    // 第二个任务应该仍然运行
    const bool second_still_running =
        second.future.wait_for(
            std::chrono::milliseconds(20)
        ) == std::future_status::timeout;

    // 再单独取消第二个任务
    second.cancellation.request_cancel();

    const bool second_ok = second.future.get() == 22;

    pool.shutdown();

    const auto stats = pool.stats();

    return first_ok &&
           second_still_running &&
           second_ok &&
           stats.submitted == 2 &&
           stats.completed == 2;
}

int main() {
    if (!test_atomic_statistics()) {
        std::cerr << "FAIL: atomic statistics\n";
        return 1;
    }

    std::cout << "atomic statistics passed\n";

    if (!test_independent_cancellation()) {
        std::cerr << "FAIL: independent cancellation\n";
        return 1;
    }

    std::cout << "independent cancellation passed\n";

    std::cout << "All Day12 ThreadPool tests passed\n";

    return 0;
}
