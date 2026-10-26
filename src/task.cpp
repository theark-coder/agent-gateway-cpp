
#include "agent/task.hpp"

namespace agent {

Result TaskService::create_task(const std::string& title) {
    if (title.empty()) {
        return {false, "task title must not be empty"};
    }

    tasks_.push_back(Task{
        next_id_,
        title,
        TaskStatus::pending
    });

    ++next_id_;

    return {true, "task created"};
}

std::optional<Task> TaskService::find_task(int id) const {
    for (const auto& task : tasks_) {
        if (task.id == id) {
            return task;
        }
    }

    return std::nullopt;
}

}  // namespace agent
