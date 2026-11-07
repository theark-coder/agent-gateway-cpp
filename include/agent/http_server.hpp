#pragma once
#include "agent/tool.hpp"
#include "agent/task.hpp"

#include <httplib.h>

#include <string>

namespace agent {

class HttpServer final {
public:
   explicit HttpServer(
    TaskService& tasks,
    ToolRegistry& tools
);

    bool listen(const std::string& host, int port);

    void stop();

private:
    httplib::Server server_;
    TaskService& tasks_;
    ToolRegistry& tools_;
};

} // namespace agent