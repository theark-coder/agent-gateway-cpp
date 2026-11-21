
#pragma once

#include "agent/tool.hpp"
#include "agent/api_key_auth.hpp"
#include "agent/task.hpp"
#include "agent/thread_pool.hpp"

#include <httplib.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace agent {

class RedisRateLimiter;

class HttpServer final {
public:
    HttpServer(
        TaskService& tasks,
        ToolRegistry& tools,
        ThreadPool& pool,
        std::shared_ptr<RedisRateLimiter> limiter = nullptr,
        std::shared_ptr<ApiKeyAuth> auth = nullptr
    );

    bool listen(const std::string& host, int port);

    void stop();

private:
    // 任务取消句柄注册表
    struct TaskControlRegistry {
        std::mutex mutex;

        std::unordered_map<
            int,
            CancellationHandle
        > handles;
    };

    httplib::Server server_;

    TaskService& tasks_;
    ToolRegistry& tools_;
    ThreadPool& pool_;
    std::shared_ptr<RedisRateLimiter> rate_limiter_;
    std::shared_ptr<ApiKeyAuth> api_key_auth_;

    // 共享管理取消句柄的生命周期
    std::shared_ptr<TaskControlRegistry> task_controls_;
};

} // namespace agent
