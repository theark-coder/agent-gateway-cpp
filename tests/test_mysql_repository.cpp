#include "agent/mysql_task_repository.hpp"
#include "agent/task.hpp"

#include <stdexcept>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

namespace {
void require(bool ok) {
    if (!ok) throw std::runtime_error("MySQL integration assertion failed");
}
std::string env_or(const char* key, const char* fallback) {
    const char* v = std::getenv(key);
    return v ? v : fallback;
}
}

int main() {
    if (env_or("AGENT_MYSQL_TEST", "") != "1") {
        std::cout << "SKIP: Set AGENT_MYSQL_TEST=1 to enable database integration tests\n";
        return 77;
    }
    try {
        agent::MySqlConfig cfg;
        cfg.host = env_or("MYSQL_HOST", "");
        cfg.user = env_or("MYSQL_USER", "agent_app");
        cfg.password = env_or("MYSQL_PASSWORD", "");
        cfg.database = env_or("MYSQL_DATABASE", "agent_gateway");
        cfg.port = static_cast<unsigned int>(std::stoul(env_or("MYSQL_PORT", "3306")));
        cfg.insecure_skip_verify = env_or("MYSQL_INSECURE_SKIP_VERIFY", "") == "1";
        cfg.ssl_ca = env_or("MYSQL_SSL_CA", "");
        if (cfg.host.empty() || cfg.password.empty()) {
            std::cerr << "MYSQL_HOST/MYSQL_PASSWORD must be set for integration test\n";
            return 2;
        }
        auto repo = std::make_shared<agent::MySqlTaskRepository>(cfg);
        agent::TaskService svc(repo);
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        const auto key = "day2-" + std::to_string(stamp);
        const auto actor = "day2-test";

        const auto first = svc.create_task("day2 mysql smoke", actor, key, "canonical-request-A");
        require(first.success && !first.replayed && first.task_id > 0);
        const auto repeat = svc.create_task("day2 mysql smoke", actor, key, "canonical-request-A");
        require(repeat.success && repeat.replayed && first.task_id == repeat.task_id);
        const auto conflict = svc.create_task("day2 mysql smoke", actor, key, "canonical-request-B");
        require(!conflict.success && conflict.conflict);
        const auto read = svc.find_task(first.task_id);
        require(read && read->title == "day2 mysql smoke" && read->status == agent::TaskStatus::pending);
        require(svc.transition_task(first.task_id, agent::TaskStatus::pending, agent::TaskStatus::running));
        require(!svc.transition_task(first.task_id, agent::TaskStatus::pending, agent::TaskStatus::completed));
        require(svc.transition_task(first.task_id, agent::TaskStatus::running, agent::TaskStatus::completed));
        const auto finished = svc.find_task(first.task_id);
        require(finished && finished->status == agent::TaskStatus::completed);
        std::cout << "PASS: MySQL persistence + prepared statements + idempotency + CAS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "MySQL integration test failed: " << e.what() << '\n';
        return 1;
    }
}
