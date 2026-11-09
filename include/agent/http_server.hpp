
#pragma once

#include "agent/tool.hpp"
#include "agent/task.hpp"
#include "agent/thread_pool.hpp"

#include <httplib.h>

#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace agent {

class HttpServer final {
public:
    HttpServer(
        TaskService& tasks,
        ToolRegistry& tools,
        ThreadPool& pool
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

    // 共享管理取消句柄的生命周期
    std::shared_ptr<TaskControlRegistry> task_controls_;
};

} // namespace agent
