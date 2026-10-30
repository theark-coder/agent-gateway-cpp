
#include "agent/thread_pool.hpp"

#include <atomic>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

int main() {
    try {
        // 1. 测试非法线程数量
        bool rejected_zero = false;

        try {
            agent::ThreadPool invalid_pool(0);
        } catch (const std::invalid_argument&) {
            rejected_zero = true;
        }

        if (!rejected_zero) {
            std::cerr << "FAIL: zero thread count accepted\n";
            return 1;
        }

        // 2. 测试任务执行和 future 返回值
        {
            agent::ThreadPool pool(4);

            std::vector<std::future<int>> futures;

            for (int i = 0; i < 100; ++i) {
                futures.push_back(
                    pool.submit([i] {
                        return i * i;
                    })
                );
            }

            for (int i = 0; i < 100; ++i) {
                if (futures[i].get() != i * i) {
                    std::cerr << "FAIL: incorrect task result\n";
                    return 1;
                }
            }

            // 3. 测试任务异常传递
            auto failed_task = pool.submit([]() -> int {
                throw std::runtime_error("expected failure");
            });

            bool exception_caught = false;

            try {
                (void)failed_task.get();
            } catch (const std::runtime_error&) {
                exception_caught = true;
            }

            if (!exception_caught) {
                std::cerr << "FAIL: exception not propagated\n";
                return 1;
            }
        }

        // 4. 测试析构时是否处理完已经接受的任务
        std::atomic<int> completed{0};

        {
            agent::ThreadPool pool(4);

            for (int i = 0; i < 100; ++i) {
                (void)pool.submit([&completed] {
                    completed.fetch_add(
                        1,
                        std::memory_order_relaxed
                    );
                });
            }
        }

        if (completed.load() != 100) {
            std::cerr << "FAIL: shutdown did not drain tasks\n";
            return 1;
        }

        std::cout << "thread pool tests passed\n";
        std::cout << "future tests passed\n";
        std::cout << "shutdown tests passed\n";

        return 0;

    } catch (const std::exception& e) {
        std::cerr << "Unexpected exception: "
                  << e.what() << '\n';
        return 1;
    }
}
