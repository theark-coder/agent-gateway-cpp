#include "agent/redis_cache.hpp"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
void check(bool yes, const char* message) {
    if (!yes) throw std::runtime_error(message);
}
std::string env(const char* name, const char* fallback) {
    const char* p = std::getenv(name);
    return p && *p ? p : fallback;
}
class FakeRepository : public agent::ITaskRepository {
public:
    std::atomic<int> reads{0};
    agent::Task task{1800000000, "Redis Day3", agent::TaskStatus::pending};
    agent::Result create_task(const std::string&, const std::string&,
                              const std::string&, const std::string&) override {
        return {true, "created", task.id};
    }
    std::optional<agent::Task> find_task(int id) override {
        ++reads;
        return id == task.id ? std::optional<agent::Task>{task} : std::nullopt;
    }
    std::vector<agent::Task> list_tasks() override { return {task}; }
    bool transition_task(int id, agent::TaskStatus expected, agent::TaskStatus next) override {
        if (id != task.id || task.status != expected) return false;
        task.status = next;
        return true;
    }
};
} // namespace

int main() {
    if (env("AGENT_REDIS_TEST", "") != "1") {
        std::cout << "SKIP: set AGENT_REDIS_TEST=1 to run live Redis tests\n";
        return 77;
    }
    try {
        agent::RedisConfig cfg;
        cfg.host = env("REDIS_HOST", "127.0.0.1");
        cfg.port = std::stoi(env("REDIS_PORT", "6380"));
        cfg.timeout_ms = 300;
        auto redis = std::make_shared<agent::RedisClient>(cfg);

        auto key = std::string("ag:day3:test:") +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
        redis->set_ex(key, "abc", 1);
        check(redis->get(key) == std::optional<std::string>{"abc"}, "SET/GET failed");
        std::this_thread::sleep_for(std::chrono::milliseconds(1150));
        check(!redis->get(key).has_value(), "TTL expiry failed");

        // Independently check the cache decorator's database hit and invalidation.
        auto db = std::make_shared<FakeRepository>();
        agent::RedisCachedTaskRepository cached(db, redis, 2);
        redis->del("ag:v1:task:1800000000");
        check(cached.find_task(1800000000)->status == agent::TaskStatus::pending, "cache miss failed");
        check(cached.find_task(1800000000)->status == agent::TaskStatus::pending, "cache hit failed");
        check(db->reads.load() == 1, "cache hit still queried backing repo");
        check(cached.transition_task(1800000000, agent::TaskStatus::pending,
                                     agent::TaskStatus::running), "CAS update failed");
        check(cached.find_task(1800000000)->status == agent::TaskStatus::running, "stale cache after update");
        check(db->reads.load() == 2, "cache invalidation did not requery DB");
        redis->del("ag:v1:task:1800000000");

        // Use unique actor to avoid crossing other test runs.
        agent::RedisRateLimiter limiter(redis, 2, 10);
        const auto actor = key;
        check(limiter.allow(actor), "first request blocked");
        check(limiter.allow(actor), "second request blocked");
        check(!limiter.allow(actor), "third request not blocked");
        redis->del("ag:v1:rl:tasks:" + actor);

        // Failure policy: read degrades to repository; limiter fails closed via exception.
        auto bad = std::make_shared<agent::RedisClient>(agent::RedisConfig{"127.0.0.1", 1, 150});
        auto fallback_db = std::make_shared<FakeRepository>();
        agent::RedisCachedTaskRepository degraded(fallback_db, bad);
        check(degraded.find_task(1800000000).has_value(), "Redis failure did not fallback to database");
        check(fallback_db->reads.load() == 1, "fallback didn't consult database");
        check(degraded.transition_task(1800000000, agent::TaskStatus::pending,
                                      agent::TaskStatus::completed), "update failed on Redis outage");
        agent::RedisRateLimiter closed(bad, 2, 10);
        bool threw = false;
        try { (void)closed.allow(actor); } catch (const std::exception&) { threw = true; }
        check(threw, "rate limiter must fail closed on Redis outage");

        std::cout << "PASS: Redis TTL / cache aside / invalidation / atomic limit / outage fallback\n";
    } catch (const std::exception& e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
    return 0;
}
