#include "agent/http_server.hpp"

#include <cstdint>

#include <nlohmann/json.hpp>
#include <chrono>
#include <thread>
#include <future>
namespace agent {

namespace {

using json = nlohmann::json;

void send_json(
    httplib::Response& res,
    int status,
    const json& body
) {
    res.status = status;
    res.set_content(
        body.dump(),
        "application/json; charset=utf-8"
    );
}

const char* status_name(TaskStatus status) {
    switch (status) {
        case TaskStatus::pending:
            return "pending";

        case TaskStatus::running:
            return "running";

        case TaskStatus::completed:
            return "completed";

        case TaskStatus::failed:
            return "failed";

        case TaskStatus::cancelled:
            return "cancelled";

        case TaskStatus::rejected:
            return "rejected";
        case TaskStatus::timed_out:
            return "timed_out";
    }

    return "unknown";
}
bool is_terminal(TaskStatus status) {
    return status == TaskStatus::completed ||
           status == TaskStatus::failed ||
           status == TaskStatus::cancelled ||
           status == TaskStatus::rejected ||
           status == TaskStatus::timed_out;
}

bool read_ms_field(
    const json& body,
    const char* key,
    int default_value,
    int max_value,
    int& output
) {
    auto it = body.find(key);

    if (it == body.end()) {
        output = default_value;
        return true;
    }

    if (it->is_number_unsigned()) {
        const auto value = it->get<std::uint64_t>();

        if (value < 1 ||
            value > static_cast<std::uint64_t>(max_value)) {
            return false;
        }

        output = static_cast<int>(value);
        return true;
    }

    if (it->is_number_integer()) {
        const auto value = it->get<std::int64_t>();

        if (value < 1 || value > max_value) {
            return false;
        }

        output = static_cast<int>(value);
        return true;
    }

    return false;
}

}
 // namespace

HttpServer::HttpServer(
    TaskService& tasks,
    ToolRegistry& tools,
    ThreadPool& pool
)
    : tasks_(tasks),
      tools_(tools),
      pool_(pool),
      task_controls_(
          std::make_shared<TaskControlRegistry>()
      ) {

    // 限制 HTTP 请求正文大小
    server_.set_payload_max_length(64 * 1024);

    // =====================================
    // GET /health
    // =====================================

    server_.Get(
        "/health",
        [](const httplib::Request&,
           httplib::Response& res) {

            send_json(
                res,
                200,
                {
                    {"status", "ok"}
                }
            );
        }
    );

    // =====================================
    // GET /api/v1/tasks
    // =====================================

    server_.Get(
        "/api/v1/tasks",
        [this](const httplib::Request&,
               httplib::Response& res) {

            json tasks = json::array();

            for (const auto& task : tasks_.list_tasks()) {
                tasks.push_back({
                    {"id", task.id},
                    {"title", task.title},
                    {"status", status_name(task.status)}
                });
            }

            send_json(
                res,
                200,
                {
                    {"tasks", tasks}
                }
            );
        }
    );
    server_.Get(
    R"(/api/v1/tasks/([0-9]+))",
    [this](const httplib::Request& req,
           httplib::Response& res) {

        int id = 0;

        try {
            id = std::stoi(req.matches[1].str());
        } catch (const std::exception&) {
            send_json(res, 400, {
                {"error", "invalid_task_id"}
            });
            return;
        }

        auto task = tasks_.find_task(id);

        if (!task) {
            send_json(res, 404, {
                {"error", "task_not_found"}
            });
            return;
        }

        send_json(res, 200, {
            {"id", task->id},
            {"title", task->title},
            {"status", status_name(task->status)}
        });
    }
);
    // =====================================
    // POST /api/v1/tasks
    // =====================================

  
server_.Post(
    "/api/v1/tasks",
    [this](const httplib::Request& req,
           httplib::Response& res) {

        // 1. 解析 JSON
        const auto body = json::parse(
            req.body, nullptr, false
        );

        if (body.is_discarded() || !body.is_object()) {
            send_json(res, 400, {
                {"error", "invalid_json"}
            });
            return;
        }

        // 2. 校验 title
        auto it = body.find("title");

        if (it == body.end() || !it->is_string()) {
            send_json(res, 400, {
                {"error", "invalid_title"}
            });
            return;
        }

        const std::string title = it->get<std::string>();

        // 3. 读取 Deadline 和模拟工作时间
        int deadline_ms = 5000;
        int work_ms = 100;

        if (!read_ms_field(
                body, "deadline_ms", 5000, 60000,
                deadline_ms)) {
            send_json(res, 400, {
                {"error", "invalid_deadline_ms"}
            });
            return;
        }

        if (!read_ms_field(
                body, "work_ms", 100, 60000,
                work_ms)) {
            send_json(res, 400, {
                {"error", "invalid_work_ms"}
            });
            return;
        }

        // 4. 创建任务
        const auto result = tasks_.create_task(title);

        if (!result.success) {
            send_json(res, 400, {
                {"error", "invalid_title"},
                {"message", result.message}
            });
            return;
        }

        const int task_id = result.task_id;

        TaskService* task_service = &tasks_;
        auto controls = task_controls_;

        // Deadline 从请求处理时开始计算
        const auto deadline =
            std::chrono::steady_clock::now() +
            std::chrono::milliseconds(deadline_ms);

        // 启动门：先注册 CancellationHandle，再允许 Worker 执行
        std::promise<void> start_promise;
        auto start_gate = start_promise.get_future().share();

        try {
            auto submitted = pool_.submit_cancellable(
                [task_service,
                 task_id,
                 controls,
                 deadline,
                 work_ms,
                 start_gate](CancellationToken token) {

                    // 等待 HTTP 线程完成取消句柄注册
                    start_gate.wait();

                    // 5. pending -> running
                    {
                        std::lock_guard<std::mutex> lock(
                            controls->mutex
                        );

                        // 任务在队列中已经被取消
                        if (token.stop_requested()) {
                            task_service->transition_task(
                                task_id,
                                TaskStatus::pending,
                                TaskStatus::cancelled
                            );

                            controls->handles.erase(task_id);
                            return;
                        }

                        // 排队期间已经超过 Deadline
                        if (std::chrono::steady_clock::now()
                            >= deadline) {

                            task_service->transition_task(
                                task_id,
                                TaskStatus::pending,
                                TaskStatus::timed_out
                            );

                            controls->handles.erase(task_id);
                            return;
                        }

                        if (!task_service->transition_task(
                                task_id,
                                TaskStatus::pending,
                                TaskStatus::running)) {

                            controls->handles.erase(task_id);
                            return;
                        }
                    }

                    // 6. 执行任务，周期检查取消与 Deadline
                    try {
                        const auto work_end =
                            std::chrono::steady_clock::now() +
                            std::chrono::milliseconds(work_ms);

                        while (
                            std::chrono::steady_clock::now() < work_end
                        ) {
                            if (token.stop_requested()) {
                                break;
                            }

                            if (std::chrono::steady_clock::now()
                                >= deadline) {
                                break;
                            }

                            std::this_thread::sleep_for(
                                std::chrono::milliseconds(5)
                            );
                        }

                    } catch (...) {
                        std::lock_guard<std::mutex> lock(
                            controls->mutex
                        );

                        task_service->transition_task(
                            task_id,
                            TaskStatus::running,
                            TaskStatus::failed
                        );

                        controls->handles.erase(task_id);
                        return;
                    }

                    // 7. 最终状态转换
                    {
                        std::lock_guard<std::mutex> lock(
                            controls->mutex
                        );

                        TaskStatus final_status =
                            TaskStatus::completed;

                        if (token.stop_requested()) {
                            final_status = TaskStatus::cancelled;
                        } else if (
                            std::chrono::steady_clock::now()
                            >= deadline
                        ) {
                            final_status = TaskStatus::timed_out;
                        }

                        task_service->transition_task(
                            task_id,
                            TaskStatus::running,
                            final_status
                        );

                        controls->handles.erase(task_id);
                    }
                }
            );

            // 8. 保存取消句柄
            {
                std::lock_guard<std::mutex> lock(
                    controls->mutex
                );

                controls->handles.emplace(
                    task_id,
                    submitted.cancellation
                );
            }

            // 9. 句柄注册完成，允许 Worker 开始执行
            start_promise.set_value();

        } catch (const std::exception&) {

            // 如果提交失败，或者句柄注册失败，
            // 先拒绝任务，再释放可能正在等待的 Worker
            tasks_.transition_task(
                task_id,
                TaskStatus::pending,
                TaskStatus::rejected
            );

            {
                std::lock_guard<std::mutex> lock(
                    controls->mutex
                );

                controls->handles.erase(task_id);
            }

            try {
                start_promise.set_value();
            } catch (const std::future_error&) {
                // 启动门已经释放
            }

            send_json(res, 503, {
                {"error", "task_queue_unavailable"}
            });

            return;
        }

        // 10. 返回 HTTP 202
        send_json(res, 202, {
            {"id", task_id},
            {"title", title},
            {"status", "accepted"},
            {"deadline_ms", deadline_ms},
            {"status_url",
             "/api/v1/tasks/" + std::to_string(task_id)}
        });
    }
);

server_.Post(
    R"(/api/v1/tasks/([0-9]+)/cancel)",
    [this](
        const httplib::Request& req,
        httplib::Response& res
    ) {

        int id = 0;

        try {
            id = std::stoi(req.matches[1].str());
        } catch (const std::exception&) {
            send_json(res, 400, {
                {"error", "invalid_task_id"}
            });
            return;
        }

        // 同一任务的取消与最终状态转换统一串行化
        std::lock_guard<std::mutex> lock(
            task_controls_->mutex
        );

        auto task = tasks_.find_task(id);

        if (!task) {
            send_json(res, 404, {
                {"error", "task_not_found"}
            });
            return;
        }

        if (is_terminal(task->status)) {
            send_json(res, 409, {
                {"error", "task_not_cancellable"},
                {"status", status_name(task->status)}
            });
            return;
        }

        auto it = task_controls_->handles.find(id);

        if (it == task_controls_->handles.end()) {
            send_json(res, 409, {
                {"error", "task_control_unavailable"}
            });
            return;
        }

        // 发出协作式取消请求
        it->second.request_cancel();

        // 情况一：任务尚未开始
        if (tasks_.transition_task(
                id,
                TaskStatus::pending,
                TaskStatus::cancelled)) {

            task_controls_->handles.erase(it);

            send_json(res, 200, {
                {"id", id},
                {"status", "cancelled"}
            });

            return;
        }

        // 情况二：任务正在执行
        auto latest = tasks_.find_task(id);

        if (latest &&
            latest->status == TaskStatus::running) {

            send_json(res, 202, {
                {"id", id},
                {"status", "cancellation_requested"}
            });

            return;
        }

        // 情况三：状态已经发生变化
        if (latest && is_terminal(latest->status)) {

            task_controls_->handles.erase(it);

            send_json(res, 409, {
                {"error", "task_not_cancellable"},
                {"status", status_name(latest->status)}
            });

            return;
        }

        send_json(res, 409, {
            {"error", "task_state_conflict"}
        });
    }
);

    // =====================================
    // POST /api/v1/tools/{tool_name}
    // =====================================

    server_.Post(
        R"(/api/v1/tools/([a-zA-Z0-9_-]+))",
        [this](const httplib::Request& req,
            httplib::Response& res) {

            // 1. 获取工具名称
            const std::string tool_name =
                req.matches[1].str();

            // 2. 解析 JSON
            const auto body = json::parse(
                req.body,
                nullptr,
                false
            );

            if (body.is_discarded() || !body.is_object()) {
                send_json(res, 400, {
                    {"error", "invalid_json"}
                });
                return;
            }

            // 3. 检查 input
            auto it = body.find("input");

            if (it == body.end() || !it->is_string()) {
                send_json(res, 400, {
                    {"error", "invalid_input"}
                });
                return;
            }

            const std::string input =
                it->get<std::string>();

            if (input.empty()) {
                send_json(res, 400, {
                    {"error", "invalid_input"}
                });
                return;
            }

            // 4. 检查工具是否存在
            if (!tools_.has_tool(tool_name)) {
                send_json(res, 404, {
                    {"error", "unknown_tool"}
                });
                return;
            }

            // 5. 执行工具
            const std::string output =
                tools_.execute(tool_name, input);

            // 6. 处理工具执行错误
            if (output.rfind("error:", 0) == 0) {
                send_json(res, 422, {
                    {"error", "tool_execution_failed"}
                });
                return;
            }

            // 7. 返回成功结果
            send_json(res, 200, {
                {"tool", tool_name},
                {"output", output}
            });
        }
    );
    // =====================================
    // 统一错误响应
    // =====================================

    server_.set_error_handler(
    [](const httplib::Request&,
       httplib::Response& res) {

        // 路由已经生成错误响应时，直接保留
        if (!res.body.empty()) {
            return;
        }

        // 未匹配的路由
        if (res.status == 404) {
            send_json(
                res,
                404,
                {{"error", "not_found"}}
            );
            return;
        }

        // 其他尚未生成响应正文的 HTTP 错误
        send_json(
            res,
            res.status,
            {
                {"error", "http_error"},
                {"status", res.status}
            }
        );
    }
);
}

bool HttpServer::listen(
    const std::string& host,
    int port
) {
    return server_.listen(host, port);
}

void HttpServer::stop() {
    server_.stop();
}

} // namespace agent