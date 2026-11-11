#include "agent/task.hpp"
#include <atomic>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

int main() {
    agent::TaskService service;
    constexpr int worker_count = 12;
    std::vector<std::thread> workers;
    std::vector<agent::Result> results(worker_count);
    for (int i = 0; i < worker_count; ++i) {
        workers.emplace_back([&, i] {
            results[i] = service.create_task("sample", "local-dev", "key-1", "req-A");
        });
    }
    for (auto& t : workers) t.join();
    for (auto& result : results) {
        if (!result.success || result.task_id != 1) return 1;
    }
    if (service.list_tasks().size() != 1) return 2;
    auto conflict = service.create_task("sample", "local-dev", "key-1", "req-B");
    if (!conflict.conflict || conflict.success) return 3;
    if (!service.transition_task(1, agent::TaskStatus::pending, agent::TaskStatus::running)) return 4;
    if (service.transition_task(1, agent::TaskStatus::pending, agent::TaskStatus::completed)) return 5;
    if (!service.transition_task(1, agent::TaskStatus::running, agent::TaskStatus::completed)) return 6;
    std::cout << "PASS: in-memory idempotency and state transition\n";
    return 0;
}
