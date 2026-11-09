
#include "agent/task.hpp"

namespace agent {

Result TaskService::create_task(const std::string& title) {
    if (title.empty()) {
        return {false, "task title must not be empty"};
    }

    std::lock_guard<std::mutex> lock(mutex_);

    const int id = next_id_;

    tasks_.push_back(Task{
        id,
        title,
        TaskStatus::pending
    });

    ++next_id_;

    return {true, "task created", id};
}

std::optional<Task> TaskService::find_task(int id) const {
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& task : tasks_) {
        if (task.id == id) {
            return task;
        }
    }

    return std::nullopt;
}
std::vector<Task> TaskService::list_tasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_;
}
bool TaskService::transition_task(
    int id,
    TaskStatus expected,
    TaskStatus next
) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& task : tasks_) {
        if (task.id != id) {
            continue;
        }

        if (task.status != expected) {
            return false;
        }

        bool allowed = false;
        if (expected == TaskStatus::pending) {
            allowed =
                next == TaskStatus::running ||
                next == TaskStatus::rejected ||
                next == TaskStatus::cancelled ||
                next == TaskStatus::timed_out;

        } else if (expected == TaskStatus::running) {
            allowed =
                next == TaskStatus::completed ||
                next == TaskStatus::failed ||
                next == TaskStatus::cancelled ||
                next == TaskStatus::timed_out;
        }

        if (!allowed) {
            return false;
        }

        task.status = next;
        return true;
    }

    return false;
}

} // namespace agent
