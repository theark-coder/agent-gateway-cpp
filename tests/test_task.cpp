
#include "agent/task.hpp"

#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main() {
    agent::TaskService service;

    // 1. 空标题必须被拒绝
    const auto invalid = service.create_task("");

    if (invalid.success) {
        std::cerr << "FAIL: empty title was accepted\n";
        return 1;
    }

    // 2. 合法任务创建成功
    const auto created = service.create_task("Learn C++");

    if (!created.success) {
        std::cerr << "FAIL: task creation failed\n";
        return 1;
    }

    // 3. 查询并检查任务字段
    const auto first = service.find_task(1);

    if (!first.has_value() ||
        first->id != 1 ||
        first->title != "Learn C++" ||
        first->status != agent::TaskStatus::pending) {
        std::cerr << "FAIL: task 1 is incorrect\n";
        return 1;
    }

    // 4. 创建第二个任务
    const auto second_created = service.create_task("Learn CMake");

    if (!second_created.success) {
        std::cerr << "FAIL: second task creation failed\n";
        return 1;
    }

    const auto second = service.find_task(2);

    if (!second.has_value() ||
        second->title != "Learn CMake") {
        std::cerr << "FAIL: task 2 is incorrect\n";
        return 1;
    }

    // 5. 不存在的任务应该返回空值
    const auto missing = service.find_task(999);

    if (missing.has_value()) {
        std::cerr << "FAIL: nonexistent task was found\n";
        return 1;
    }

    // 6. 并发创建 1000 个任务
    constexpr int task_count = 1000;
    constexpr int worker_count = 8;

    agent::TaskService concurrent_service;

    std::vector<std::thread> workers;
    std::vector<int> create_ok(task_count, 0);

    std::atomic<int> next_index{0};

    for (int w = 0; w < worker_count; ++w) {
        workers.emplace_back([&] {
            while (true) {
                int i = next_index.fetch_add(1);

                if (i >= task_count) {
                    break;
                }

                const auto result =
                    concurrent_service.create_task(
                        "task-" + std::to_string(i)
                    );

                create_ok[i] = result.success ? 1 : 0;
            }
        });
    }

    // 7. 等待所有工作线程完成
    for (auto& worker : workers) {
        worker.join();
    }

    // 8. 检查 1000 次创建是否全部成功
    for (int ok : create_ok) {
        if (!ok) {
            std::cerr << "FAIL: concurrent creation failed\n";
            return 1;
        }
    }

    // 9. 检查 ID 是否连续且没有遗漏
    for (int id = 1; id <= task_count; ++id) {
        const auto task = concurrent_service.find_task(id);

        if (!task.has_value() || task->id != id) {
            std::cerr << "FAIL: missing or invalid task id "
                      << id << '\n';
            return 1;
        }
    }

    // 10. 检查并发任务之外的 ID 不存在
    const auto missing_concurrent =
        concurrent_service.find_task(task_count + 1);

    if (missing_concurrent.has_value()) {
        std::cerr << "FAIL: unexpected task found\n";
        return 1;
    }

    std::cout << "task tests passed\n";
    std::cout << "concurrent task tests passed\n";

    return 0;
}
