#pragma once

#include "agent/redis_client.hpp"
#include "agent/task.hpp"

#include <array>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace agent {

// Single-process cache-aside decorator; MySQL (or wrapped repository) is authoritative.
class RedisCachedTaskRepository final : public ITaskRepository {
public:
    RedisCachedTaskRepository(std::shared_ptr<ITaskRepository> backing,
                              std::shared_ptr<RedisClient> redis,
                              int ttl_seconds = 60);

    Result create_task(const std::string& title,
                       const std::string& actor_id,
                       const std::string& key,
                       const std::string& fingerprint) override;
    std::optional<Task> find_task(int id) override;
    std::vector<Task> list_tasks() override;
    bool transition_task(int id, TaskStatus expected, TaskStatus next) override;

private:
    std::string cache_key(int id) const;
    std::mutex& stripe(int id);
    std::shared_ptr<ITaskRepository> backing_;
    std::shared_ptr<RedisClient> redis_;
    int ttl_seconds_;
    std::array<std::mutex, 64> stripes_{};
};

// Local development: one fixed actor. Authentication must supply the actor in production.
class RedisRateLimiter final {
public:
    RedisRateLimiter(std::shared_ptr<RedisClient> redis,
                     int max_requests = 120, int window_seconds = 60);
    bool allow(const std::string& actor_id);
private:
    std::shared_ptr<RedisClient> redis_;
    int max_requests_;
    int window_seconds_;
};

} // namespace agent
