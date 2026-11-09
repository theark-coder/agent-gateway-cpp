#include "agent/http_server.hpp"
#include "agent/task.hpp"
#include "agent/tool.hpp"
#include <memory>
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

        agent::TaskService tasks;

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