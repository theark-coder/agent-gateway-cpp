
#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace agent {

enum class TaskStatus {
    pending,
    completed,
    running,
    failed,
    cancelled,
    rejected,
    timed_out
};

struct Task {
    int id;
    std::string title;
    TaskStatus status = TaskStatus::pending;
};

struct Result {
    bool success;
    std::string message;
    int task_id = 0;
};

class TaskService {
public:
    Result create_task(const std::string& title);

    std::optional<Task> find_task(int id) const;
    std::vector<Task> list_tasks() const;
    // 使用 expected 防止任务状态被错误覆盖
    bool transition_task(
        int id,
        TaskStatus expected,
        TaskStatus next
    );

private:
    mutable std::mutex mutex_;
    std::vector<Task> tasks_;
    int next_id_ = 1;
};

} // namespace agent
