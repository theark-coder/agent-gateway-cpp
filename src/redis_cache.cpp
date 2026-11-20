#include "agent/redis_cache.hpp"

#include <nlohmann/json.hpp>

#include <stdexcept>
#include <utility>

namespace agent {
namespace {
const char* status_name(TaskStatus s) {
    switch (s) {
        case TaskStatus::pending: return "pending";
        case TaskStatus::running: return "running";
        case TaskStatus::completed: return "completed";
        case TaskStatus::failed: return "failed";
        case TaskStatus::cancelled: return "cancelled";
        case TaskStatus::rejected: return "rejected";
        case TaskStatus::timed_out: return "timed_out";
    }
    throw std::logic_error("unknown task status");
}
std::optional<TaskStatus> parse_status(const std::string& text) {
    for (TaskStatus s : {TaskStatus::pending, TaskStatus::running,
                         TaskStatus::completed, TaskStatus::failed,
                         TaskStatus::cancelled, TaskStatus::rejected,
                         TaskStatus::timed_out})
        if (text == status_name(s)) return s;
    return std::nullopt;
}
} // namespace

RedisCachedTaskRepository::RedisCachedTaskRepository(
    std::shared_ptr<ITaskRepository> backing,
    std::shared_ptr<RedisClient> redis,
    int ttl_seconds)
    : backing_(std::move(backing)), redis_(std::move(redis)), ttl_seconds_(ttl_seconds) {
    if (!backing_ || !redis_ || ttl_seconds_ < 1 || ttl_seconds_ > 86400)
        throw std::invalid_argument("invalid cached repository configuration");
}

std::string RedisCachedTaskRepository::cache_key(int id) const {
    return "ag:v1:task:" + std::to_string(id);
}

std::mutex& RedisCachedTaskRepository::stripe(int id) {
    return stripes_[static_cast<unsigned int>(id) % stripes_.size()];
}

Result RedisCachedTaskRepository::create_task(const std::string& title,
    const std::string& actor_id, const std::string& key, const std::string& fingerprint) {
    // New records are not cached until first read. A replay doesn't re-submit work.
    return backing_->create_task(title, actor_id, key, fingerprint);
}

std::optional<Task> RedisCachedTaskRepository::find_task(int id) {
    std::lock_guard<std::mutex> lock(stripe(id));
    const auto key = cache_key(id);
    try {
        auto raw = redis_->get(key);
        if (raw) {
            auto parsed = nlohmann::json::parse(*raw, nullptr, false);
            if (parsed.is_object() && parsed.value("id", -1) == id &&
                parsed.contains("title") && parsed["title"].is_string() &&
                parsed.contains("status") && parsed["status"].is_string()) {
                auto status = parse_status(parsed["status"].get<std::string>());
                if (status) return Task{id, parsed["title"].get<std::string>(), *status};
            }
            // Malformed cache entries must not hide valid MySQL data.
            redis_->del(key);
        }
    } catch (const std::exception&) {
        // Redis cache failure -> authoritative repository fallback.
    }
    auto from_db = backing_->find_task(id);
    if (from_db) {
        try {
            redis_->set_ex(key,
                nlohmann::json({{"id",from_db->id}, {"title",from_db->title},
                                {"status",status_name(from_db->status)}}).dump(), ttl_seconds_);
        } catch (const std::exception&) {
            // Cache write is best-effort.
        }
    }
    return from_db;
}

std::vector<Task> RedisCachedTaskRepository::list_tasks() {
    // List semantics and pagination remain with the authoritative database.
    return backing_->list_tasks();
}

bool RedisCachedTaskRepository::transition_task(int id, TaskStatus expected, TaskStatus next) {
    std::lock_guard<std::mutex> lock(stripe(id));
    const bool updated = backing_->transition_task(id, expected, next);
    // Invalidate after MySQL success; also evict stale cache on CAS failure.
    // A Redis failure must not roll back an already-committed MySQL update.
    try { redis_->del(cache_key(id)); } catch (const std::exception&) {}
    return updated;
}

RedisRateLimiter::RedisRateLimiter(std::shared_ptr<RedisClient> redis,
                                  int max_requests, int window_seconds)
    : redis_(std::move(redis)), max_requests_(max_requests), window_seconds_(window_seconds) {
    if (!redis_ || max_requests_ < 1 || window_seconds_ < 1)
        throw std::invalid_argument("invalid rate limiter configuration");
}

bool RedisRateLimiter::allow(const std::string& actor_id) {
    if (actor_id.empty()) throw std::invalid_argument("actor id required");
    const auto count = redis_->increment_with_expiry("ag:v1:rl:tasks:" + actor_id,
                                                      window_seconds_);
    return count <= max_requests_;
}

} // namespace agent
