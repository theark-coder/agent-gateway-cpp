
#pragma once

#include <memory>
#include <mutex>
#include <unordered_map>
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
    bool replayed = false;
    bool conflict = false;
};

class ITaskRepository {
public:
    virtual ~ITaskRepository() = default;
    virtual Result create_task(const std::string& title,
                               const std::string& actor_id,
                               const std::string& idempotency_key,
                               const std::string& fingerprint) = 0;
    virtual std::optional<Task> find_task(int id) = 0;
    virtual std::vector<Task> list_tasks() = 0;
    virtual bool transition_task(int id, TaskStatus expected, TaskStatus next) = 0;
};

class TaskService {
public:
    explicit TaskService(std::shared_ptr<ITaskRepository> repository = nullptr);
    Result create_task(const std::string& title,
                       const std::string& actor_id,
                       const std::string& idempotency_key,
                       const std::string& fingerprint);
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
    std::shared_ptr<ITaskRepository> repository_;
    std::unordered_map<std::string, std::pair<int, std::string>> idempotency_;
};

} // namespace agent
