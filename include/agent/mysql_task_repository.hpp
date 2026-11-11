#pragma once

#include "agent/task.hpp"

#include <memory>
#include <mutex>
#include <string>

struct st_mysql;

namespace agent {

struct MySqlConfig {
    std::string host;
    unsigned int port = 3306;
    std::string user;
    std::string password;
    std::string database = "agent_gateway";
    // Only for isolated local development with a self-signed server certificate.
    bool insecure_skip_verify = false;
    std::string ssl_ca;
};

class MySqlTaskRepository final : public ITaskRepository {
public:
    explicit MySqlTaskRepository(const MySqlConfig& config);
    ~MySqlTaskRepository() override;

    MySqlTaskRepository(const MySqlTaskRepository&) = delete;
    MySqlTaskRepository& operator=(const MySqlTaskRepository&) = delete;

    Result create_task(const std::string& title,
                       const std::string& actor_id,
                       const std::string& idempotency_key,
                       const std::string& fingerprint) override;
    std::optional<Task> find_task(int id) override;
    std::vector<Task> list_tasks() override;
    bool transition_task(int id, TaskStatus expected, TaskStatus next) override;

    // A process restart cannot resume tasks that were in the old thread pool.
    void recover_unfinished();

private:
    mutable std::mutex mutex_;
    st_mysql* db_ = nullptr;
};

} // namespace agent
