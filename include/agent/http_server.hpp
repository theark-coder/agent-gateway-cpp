#pragma once

#include "agent/task.hpp"

#include <httplib.h>

#include <string>

namespace agent {

class HttpServer final {
public:
    explicit HttpServer(TaskService& tasks);

    bool listen(const std::string& host, int port);

    void stop();

private:
    httplib::Server server_;
    TaskService& tasks_;
};

} // namespace agent