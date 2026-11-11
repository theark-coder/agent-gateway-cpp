#include "agent/http_server.hpp"
#include "agent/task.hpp"
#include "agent/tool.hpp"
#include <memory>
#include <cstdlib>
#ifdef AGENT_HAS_MYSQL
#include "agent/mysql_task_repository.hpp"
#endif
#include <iostream>
#include <stdexcept>
#include <string>
#include "agent/thread_pool.hpp"
int main(int argc, char* argv[]) {
    try {
        int port = 8080;
        std::string host = "127.0.0.1";

        if (argc > 1) {
            port = std::stoi(argv[1]);
        }

        if (argc > 2) {
            host = argv[2];
        }

        if (port < 1 || port > 65535) {
            throw std::invalid_argument("invalid port");
        }

        std::shared_ptr<agent::ITaskRepository> repository;
#ifdef AGENT_HAS_MYSQL
        const char* mysql_host = std::getenv("MYSQL_HOST");
        if (mysql_host && *mysql_host) {
            auto env = [](const char* name, const char* fallback = "") -> std::string {
                const char* value = std::getenv(name);
                return value ? value : fallback;
            };
            agent::MySqlConfig config;
            config.host = mysql_host;
            config.port = static_cast<unsigned int>(std::stoul(env("MYSQL_PORT", "3306")));
            config.user = env("MYSQL_USER", "agent_app");
            config.password = env("MYSQL_PASSWORD");
            config.database = env("MYSQL_DATABASE", "agent_gateway");
            config.ssl_ca = env("MYSQL_SSL_CA");
            config.insecure_skip_verify = env("MYSQL_INSECURE_SKIP_VERIFY") == "1";
            if (config.password.empty())
                throw std::runtime_error("MYSQL_PASSWORD must be set when MYSQL_HOST is set");
            auto mysql_repo = std::make_shared<agent::MySqlTaskRepository>(config);
            mysql_repo->recover_unfinished();
            repository = mysql_repo;
        }
#else
        if (std::getenv("MYSQL_HOST"))
            throw std::runtime_error("MySQL not built: install libmariadb-dev and reconfigure CMake");
#endif
        agent::TaskService tasks(repository);

        agent::ToolRegistry tools;
        // 创建 4 个工作线程，队列容量为 64

        if (!tools.register_tool(
                std::make_unique<agent::LookupPolicyTool>())) {
            throw std::runtime_error("tool registration failed");
        }
        agent::ThreadPool pool(4, 64);

        agent::HttpServer server(tasks, tools, pool);


        std::cout
            << "READY http://"
            << host << ":" << port
            << std::endl;

        if (!server.listen(host, port)) {
            std::cerr << "HTTP server failed to listen\n";
            return 1;
        }

        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "HTTP server error: "
                  << ex.what() << '\n';

        return 1;
    }
}