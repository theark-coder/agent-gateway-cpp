#include "agent/http_server.hpp"

#include <nlohmann/json.hpp>

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
    }

    return "unknown";
}

} // namespace

HttpServer::HttpServer(TaskService& tasks)
    : tasks_(tasks) {

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

    // =====================================
    // POST /api/v1/tasks
    // =====================================

    server_.Post(
        "/api/v1/tasks",
        [this](const httplib::Request& req,
               httplib::Response& res) {

            // 解析 JSON
            const auto body = json::parse(
                req.body,
                nullptr,
                false
            );

            if (body.is_discarded() || !body.is_object()) {
                send_json(
                    res,
                    400,
                    {{"error", "invalid_json"}}
                );
                return;
            }

            // 检查 title
            auto it = body.find("title");

            if (it == body.end() || !it->is_string()) {
                send_json(
                    res,
                    400,
                    {{"error", "invalid_title"}}
                );
                return;
            }

            const std::string title = it->get<std::string>();

            // 调用原有 TaskService
            const auto result = tasks_.create_task(title);

            if (!result.success) {
                send_json(
                    res,
                    400,
                    {
                        {"error", "invalid_title"},
                        {"message", result.message}
                    }
                );
                return;
            }

            // 创建成功
            send_json(
                res,
                201,
                {
                    {"id", result.task_id},
                    {"title", title},
                    {"status", "pending"}
                }
            );
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