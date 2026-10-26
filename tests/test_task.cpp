
#include "agent/task.hpp"

#include <iostream>

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

    if (!first.has_value()) {
        std::cerr << "FAIL: task 1 not found\n";
        return 1;
    }

    if (first->id != 1 ||
        first->title != "Learn C++" ||
        first->status != agent::TaskStatus::pending) {
        std::cerr << "FAIL: task fields are incorrect\n";
        return 1;
    }

    // 4. 创建第二个任务，验证 ID 递增
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

    std::cout << "task tests passed\n";
    return 0;
}
