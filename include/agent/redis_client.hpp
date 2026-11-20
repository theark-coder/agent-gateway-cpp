#pragma once

#include <mutex>
#include <optional>
#include <string>
#include <vector>

struct redisContext;
struct redisReply;

namespace agent {

struct RedisConfig {
    std::string host = "127.0.0.1";
    int port = 6380;
    int timeout_ms = 300;
};

// Thread-safe, lazy reconnecting hiredis wrapper.
// Commands throw on Redis failure; callers decide their fallback policy.
class RedisClient final {
public:
    explicit RedisClient(RedisConfig config);
    ~RedisClient();
    RedisClient(const RedisClient&) = delete;
    RedisClient& operator=(const RedisClient&) = delete;

    std::optional<std::string> get(const std::string& key);
    void set_ex(const std::string& key, const std::string& value, int seconds);
    void del(const std::string& key);
    long long increment_with_expiry(const std::string& key, int seconds);

private:
    // Caller holds mutex_. All command argument strings remain alive until reply.
    redisReply* command_locked(const std::vector<std::string>& args);
    void connect_locked();
    void disconnect_locked() noexcept;

    RedisConfig config_;
    std::mutex mutex_;
    redisContext* context_ = nullptr;
};

} // namespace agent
