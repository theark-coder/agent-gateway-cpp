#include "agent/redis_client.hpp"

#include <hiredis/hiredis.h>

#include <memory>
#include <stdexcept>
#include <sys/time.h>
#include <utility>

namespace agent {
namespace {
using Reply = std::unique_ptr<redisReply, void(*)(void*)>;
Reply own(redisReply* reply) { return Reply(reply, freeReplyObject); }
void require_ok(const redisReply* reply) {
    if (reply->type == REDIS_REPLY_ERROR) {
        throw std::runtime_error(std::string("Redis error: ") + (reply->str ? reply->str : "unknown"));
    }
}
} // namespace

RedisClient::RedisClient(RedisConfig config) : config_(std::move(config)) {
    if (config_.host.empty() || config_.port < 1 || config_.port > 65535 ||
        config_.timeout_ms < 1 || config_.timeout_ms > 10000)
        throw std::invalid_argument("invalid Redis config");
}

RedisClient::~RedisClient() {
    std::lock_guard<std::mutex> lock(mutex_);
    disconnect_locked();
}

void RedisClient::disconnect_locked() noexcept {
    if (context_) redisFree(context_);
    context_ = nullptr;
}

void RedisClient::connect_locked() {
    if (context_ && context_->err == 0) return;
    disconnect_locked();
    struct timeval tv {config_.timeout_ms / 1000,
                       (config_.timeout_ms % 1000) * 1000};
    context_ = redisConnectWithTimeout(config_.host.c_str(), config_.port, tv);
    if (!context_) throw std::runtime_error("Redis connection allocation failed");
    if (context_->err) {
        const std::string why = context_->errstr;
        disconnect_locked();
        throw std::runtime_error("Redis connect: " + why);
    }
    if (redisSetTimeout(context_, tv) != REDIS_OK) {
        disconnect_locked();
        throw std::runtime_error("Redis timeout setup failed");
    }
}

redisReply* RedisClient::command_locked(const std::vector<std::string>& args) {
    connect_locked();
    std::vector<const char*> argv;
    std::vector<size_t> sizes;
    argv.reserve(args.size());
    sizes.reserve(args.size());
    for (const auto& arg : args) {
        argv.push_back(arg.data());
        sizes.push_back(arg.size());
    }
    auto* result = static_cast<redisReply*>(redisCommandArgv(
        context_, static_cast<int>(argv.size()), argv.data(), sizes.data()));
    if (!result) {
        const std::string why = context_ ? context_->errstr : "unknown";
        disconnect_locked();
        throw std::runtime_error("Redis command failed: " + why);
    }
    return result;
}

std::optional<std::string> RedisClient::get(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto reply = own(command_locked({"GET", key}));
    require_ok(reply.get());
    if (reply->type == REDIS_REPLY_NIL) return std::nullopt;
    if (reply->type != REDIS_REPLY_STRING) throw std::runtime_error("Redis GET unexpected reply");
    return std::string(reply->str, static_cast<size_t>(reply->len));
}

void RedisClient::set_ex(const std::string& key, const std::string& value, int seconds) {
    if (seconds <= 0) throw std::invalid_argument("TTL must be positive");
    std::lock_guard<std::mutex> lock(mutex_);
    auto reply = own(command_locked({"SET", key, value, "EX", std::to_string(seconds)}));
    require_ok(reply.get());
    if (reply->type != REDIS_REPLY_STATUS || std::string(reply->str) != "OK")
        throw std::runtime_error("Redis SET unexpected reply");
}

void RedisClient::del(const std::string& key) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto reply = own(command_locked({"DEL", key}));
    require_ok(reply.get());
    if (reply->type != REDIS_REPLY_INTEGER) throw std::runtime_error("Redis DEL unexpected reply");
}

long long RedisClient::increment_with_expiry(const std::string& key, int seconds) {
    if (seconds <= 0) throw std::invalid_argument("window must be positive");
    // Atomic: increment and attach an expiry only on the first increment.
    constexpr const char* script =
        "local n=redis.call('INCR',KEYS[1]); "
        "if n==1 then redis.call('EXPIRE',KEYS[1],ARGV[1]); end; return n";
    std::lock_guard<std::mutex> lock(mutex_);
    auto reply = own(command_locked({"EVAL", script, "1", key, std::to_string(seconds)}));
    require_ok(reply.get());
    if (reply->type != REDIS_REPLY_INTEGER)
        throw std::runtime_error("Redis rate limit unexpected reply");
    return reply->integer;
}

} // namespace agent
