#include "agent/python_model_adapter.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <csignal>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>

#include <fcntl.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace agent {
namespace {

class Child final {
public:
    explicit Child(const std::string& script) {
        int sockets[2]{};
        if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) != 0)
            throw std::runtime_error("socketpair failed");
        pid_ = ::fork();
        if (pid_ < 0) {
            ::close(sockets[0]); ::close(sockets[1]);
            throw std::runtime_error("fork failed");
        }
        if (pid_ == 0) {
            ::close(sockets[0]);
            if (::dup2(sockets[1], STDIN_FILENO) == -1 ||
                ::dup2(sockets[1], STDOUT_FILENO) == -1) _exit(127);
            ::close(sockets[1]);
            ::execlp("python3", "python3", "-u", script.c_str(),
                     static_cast<char*>(nullptr));
            _exit(127);
        }
        fd_ = sockets[0];
        ::close(sockets[1]);
        const int flags = ::fcntl(fd_, F_GETFL, 0);
        if (flags < 0 || ::fcntl(fd_, F_SETFL, flags | O_NONBLOCK) < 0)
            throw std::runtime_error("fcntl failed");
    }
    Child(const Child&) = delete;
    Child& operator=(const Child&) = delete;
    ~Child() {
        if (fd_ != -1) ::close(fd_);
        if (pid_ > 0) {
            int status = 0;
            pid_t done = ::waitpid(pid_, &status, WNOHANG);
            if (done == 0) {
                ::kill(pid_, SIGKILL);
                do { done = ::waitpid(pid_, &status, 0); } while (done == -1 && errno == EINTR);
            }
        }
    }
    int fd() const { return fd_; }
private:
    pid_t pid_ = -1;
    int fd_ = -1;
};

int remaining_ms(std::chrono::steady_clock::time_point deadline) {
    const auto diff = std::chrono::duration_cast<std::chrono::milliseconds>(
        deadline - std::chrono::steady_clock::now()).count();
    if (diff < 0) return 0;
    return static_cast<int>(std::min<std::int64_t>(diff + 1, 1000000));
}

void wait_fd(int fd, short events, std::chrono::steady_clock::time_point deadline) {
    for (;;) {
        const int timeout = remaining_ms(deadline);
        if (timeout == 0) throw std::runtime_error("model deadline exceeded");
        pollfd pfd{fd, events, 0};
        const int rc = ::poll(&pfd, 1, timeout);
        if (rc > 0) {
            if (pfd.revents & (POLLERR | POLLNVAL)) throw std::runtime_error("model pipe error");
            return;
        }
        if (rc == 0) throw std::runtime_error("model deadline exceeded");
        if (errno != EINTR) throw std::runtime_error("model poll failed");
    }
}

std::string exchange(const std::string& script, const std::string& input,
                     std::chrono::steady_clock::time_point deadline) {
    Child child(script);
    const int fd = child.fd();
    std::size_t sent = 0;
    while (sent < input.size()) {
        wait_fd(fd, POLLOUT, deadline);
        const ssize_t n = ::send(fd, input.data() + sent, input.size() - sent,
                                 MSG_DONTWAIT | MSG_NOSIGNAL);
        if (n > 0) sent += static_cast<std::size_t>(n);
        else if (n == -1 && (errno == EINTR || errno == EAGAIN)) continue;
        else throw std::runtime_error("model input write failed");
    }
    ::shutdown(fd, SHUT_WR);
    std::string output;
    char buffer[4096];
    for (;;) {
        wait_fd(fd, POLLIN, deadline);
        const ssize_t n = ::recv(fd, buffer, sizeof(buffer), MSG_DONTWAIT);
        if (n == 0) throw std::runtime_error("model closed without response");
        if (n == -1 && (errno == EINTR || errno == EAGAIN)) continue;
        if (n < 0) throw std::runtime_error("model output read failed");
        output.append(buffer, static_cast<std::size_t>(n));
        if (output.size() > 65536) throw std::runtime_error("model response too large");
        const auto newline = output.find('\n');
        if (newline != std::string::npos) return output.substr(0, newline);
    }
}

} // namespace

PythonModelAdapter::PythonModelAdapter(std::string script_path)
    : script_path_(std::move(script_path)) {
    if (script_path_.empty()) throw std::invalid_argument("script path is empty");
}

ModelDecision PythonModelAdapter::decide(const std::string& prompt, int step,
                                        const std::string& tool_result,
                                        const std::vector<MemoryMessage>& history,
                                        std::chrono::steady_clock::time_point deadline) {
    nlohmann::json request = { {"prompt", prompt}, {"step", step} };
    if (step > 0) request["tool_result"] = tool_result;
    nlohmann::json prior = nlohmann::json::array();
    const std::size_t start = history.size() > 6 ? history.size() - 6 : 0;
    for (std::size_t i = start; i < history.size(); ++i) {
        prior.push_back({{"role", history[i].role},
                         {"content", history[i].content.substr(0, 256)}});
    }
    request["history"] = std::move(prior);
    const auto response_text = exchange(script_path_, request.dump() + "\n", deadline);
    const auto response = nlohmann::json::parse(response_text);
    if (!response.is_object() || !response.contains("type") ||
        !response["type"].is_string()) throw std::runtime_error("invalid model response");
    const auto type = response["type"].get<std::string>();
    ModelDecision decision;
    if (type == "final") {
        decision.action = ModelAction::final_answer;
        decision.content = response.at("content").get<std::string>();
    } else if (type == "tool_call") {
        decision.action = ModelAction::tool_call;
        decision.tool_name = response.at("name").get<std::string>();
        decision.tool_input = response.at("arguments").at("input").get<std::string>();
    } else {
        throw std::runtime_error("model returned an error or unknown action");
    }
    return decision;
}

} // namespace agent
