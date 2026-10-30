
#include "agent/bounded_queue.hpp"

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>
#include <vector>

// 测试 1：容量限制与关闭后排空
bool test_capacity_and_drain() {
    agent::BoundedQueue<int> queue(2);

    if (!queue.try_push(10)) return false;
    if (!queue.try_push(20)) return false;

    // 容量已满，第三次必须失败
    if (queue.try_push(30)) return false;

    queue.close();

    // 关闭后禁止继续提交
    if (queue.try_push(40)) return false;

    auto a = queue.pop();
    auto b = queue.pop();
    auto c = queue.pop();

    return a.has_value() && *a == 10 &&
           b.has_value() && *b == 20 &&
           !c.has_value();
}

// 测试 2：拒绝零容量
bool test_zero_capacity() {
    try {
        agent::BoundedQueue<int> queue(0);
    } catch (const std::invalid_argument&) {
        return true;
    }

    return false;
}

// 测试 3：close 唤醒等待中的消费者
bool test_close_wakes_consumers() {
    agent::BoundedQueue<int> queue(1);

    constexpr int consumer_count = 4;

    std::atomic<int> started{0};
    std::atomic<int> exited{0};

    std::vector<std::thread> consumers;

    for (int i = 0; i < consumer_count; ++i) {
        consumers.emplace_back([&] {
            started.fetch_add(1);

            auto result = queue.pop();

            if (!result.has_value()) {
                exited.fetch_add(1);
            }
        });
    }

    // 等待所有消费者启动
    while (started.load() != consumer_count) {
        std::this_thread::yield();
    }

    // 给消费者时间进入等待状态
    std::this_thread::sleep_for(
        std::chrono::milliseconds(50)
    );

    queue.close();

    for (auto& consumer : consumers) {
        consumer.join();
    }

    return exited.load() == consumer_count;
}

// 测试 4：多生产者、多消费者并发
bool test_multi_producer_consumer() {
    constexpr int producer_count = 4;
    constexpr int consumer_count = 4;
    constexpr int items_per_producer = 500;

    constexpr int total =
        producer_count * items_per_producer;

    agent::BoundedQueue<int> queue(32);

    std::unique_ptr<std::atomic<int>[]> seen(
        new std::atomic<int>[total]
    );

    for (int i = 0; i < total; ++i) {
        seen[i].store(0);
    }

    std::atomic<int> consumed{0};
    std::atomic<bool> invalid{false};

    std::vector<std::thread> consumers;
    std::vector<std::thread> producers;

    // 启动消费者
    for (int i = 0; i < consumer_count; ++i) {
        consumers.emplace_back([&] {
            while (true) {
                auto item = queue.pop();

                if (!item.has_value()) {
                    break;
                }

                if (*item < 0 || *item >= total) {
                    invalid.store(true);
                    continue;
                }

                seen[*item].fetch_add(1);
                consumed.fetch_add(1);
            }
        });
    }

    // 启动生产者
    for (int p = 0; p < producer_count; ++p) {
        producers.emplace_back([&, p] {
            for (int i = 0; i < items_per_producer; ++i) {
                int id = p * items_per_producer + i;

                // 队列满时重试，直到成功
                while (!queue.try_push(id)) {
                    std::this_thread::yield();
                }
            }
        });
    }

    // 等待全部生产者完成
    for (auto& producer : producers) {
        producer.join();
    }

    // 所有生产者完成后关闭队列
    queue.close();

    // 消费者排空后退出
    for (auto& consumer : consumers) {
        consumer.join();
    }

    if (invalid.load() || consumed.load() != total) {
        return false;
    }

    // 每个任务必须恰好消费一次
    for (int i = 0; i < total; ++i) {
        if (seen[i].load() != 1) {
            return false;
        }
    }

    return true;
}

int main() {
    if (!test_zero_capacity()) {
        std::cerr << "FAIL: zero capacity\n";
        return 1;
    }

    if (!test_capacity_and_drain()) {
        std::cerr << "FAIL: capacity and drain\n";
        return 1;
    }

    if (!test_close_wakes_consumers()) {
        std::cerr << "FAIL: close wakeup\n";
        return 1;
    }

    if (!test_multi_producer_consumer()) {
        std::cerr << "FAIL: multi producer consumer\n";
        return 1;
    }

    std::cout << "zero capacity test passed\n";
    std::cout << "capacity and drain tests passed\n";
    std::cout << "close wakeup tests passed\n";
    std::cout << "multi producer consumer tests passed\n";

    return 0;
}
