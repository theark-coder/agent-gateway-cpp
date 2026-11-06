#include "agent/http_server.hpp"
#include "agent/task.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

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
        agent::HttpServer server(tasks);

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