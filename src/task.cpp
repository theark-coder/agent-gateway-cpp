
#include "agent/task.hpp"
#include <utility>

namespace agent {

TaskService::TaskService(std::shared_ptr<ITaskRepository> repository)
    : repository_(std::move(repository)) {}

Result TaskService::create_task(const std::string& title) {
    return create_task(title, "local-dev", "", title);
}

Result TaskService::create_task(const std::string& title,
                               const std::string& actor_id,
                               const std::string& key,
                               const std::string& fingerprint) {
    if (title.empty() || title.size() > 255) {
        return {false, "invalid_title"};
    }
    if (actor_id.empty() || actor_id.size() > 64 || key.size() > 128 ||
        fingerprint.size() > 512) {
        return {false, "invalid_task_metadata"};
    }
    if (repository_) return repository_->create_task(title, actor_id, key, fingerprint);

    std::lock_guard<std::mutex> lock(mutex_);
    const std::string unique = actor_id + "\x1f" + key;
    if (!key.empty()) {
        auto it = idempotency_.find(unique);
        if (it != idempotency_.end()) {
            if (it->second.second != fingerprint)
                return {false, "idempotency_conflict", it->second.first, false, true};
            return {true, "idempotent replay", it->second.first, true, false};
        }
    }
    const int id = next_id_;
    tasks_.push_back(Task{id, title, TaskStatus::pending});
    ++next_id_;
    if (!key.empty()) idempotency_.emplace(unique, std::make_pair(id, fingerprint));
    return {true, "task created", id};
}

std::optional<Task> TaskService::find_task(int id) const {
    if (repository_) return repository_->find_task(id);
    std::lock_guard<std::mutex> lock(mutex_);

    for (const auto& task : tasks_) {
        if (task.id == id) {
            return task;
        }
    }

    return std::nullopt;
}
std::vector<Task> TaskService::list_tasks() const {
    if (repository_) return repository_->list_tasks();
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_;
}
bool TaskService::transition_task(
    int id,
    TaskStatus expected,
    TaskStatus next
) {
    bool allowed = false;
    if (expected == TaskStatus::pending)
        allowed = next == TaskStatus::running || next == TaskStatus::rejected ||
                  next == TaskStatus::cancelled || next == TaskStatus::timed_out;
    else if (expected == TaskStatus::running)
        allowed = next == TaskStatus::completed || next == TaskStatus::failed ||
                  next == TaskStatus::cancelled || next == TaskStatus::timed_out;
    if (!allowed) return false;
    if (repository_) return repository_->transition_task(id, expected, next);

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
