
#pragma once

#include <optional>
#include <string>
#include <vector>

namespace agent {

enum class TaskStatus {
    pending,
    completed
};

struct Task {
    int id;
    std::string title;
    TaskStatus status = TaskStatus::pending;
};

struct Result {
    bool success;
    std::string message;
};

class TaskService {
public:
    Result create_task(const std::string& title);

    std::optional<Task> find_task(int id) const;

private:
    std::vector<Task> tasks_;
    int next_id_ = 1;
};

}  // namespace agent
