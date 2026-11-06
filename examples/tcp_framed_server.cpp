#include "agent/unique_fd.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unordered_map>
#include <utility>
#include <unistd.h>

namespace {

constexpr std::size_t kHeaderBytes = sizeof(std::uint32_t);
constexpr std::size_t kMaxFrame = 64 * 1024;
constexpr std::size_t kMaxInput = kMaxFrame + kHeaderBytes + 4096;
constexpr std::size_t kHighWater = 128 * 1024;
constexpr std::size_t kLowWater = 64 * 1024;
constexpr std::size_t kMaxOutput = 256 * 1024;

struct ProtocolError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

void check(bool ok, const char* operation) {
    if (!ok) {
        throw std::system_error(errno, std::generic_category(), operation);
    }
}

void make_nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    check(flags >= 0, "fcntl F_GETFL");
    check(::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0, "fcntl F_SETFL");
}

void watch(int epfd, int op, int fd, std::uint32_t flags) {
    epoll_event ev{};
    ev.events = flags;
    ev.data.fd = fd;
    check(::epoll_ctl(epfd, op, fd, &ev) == 0, "epoll_ctl");
}

struct Client {
    agent::UniqueFd fd;
    std::string input;   // incomplete TCP frame(s)
    std::string output;  // encoded frames awaiting send
    std::size_t sent = 0;
    bool read_eof = false;
    bool read_paused = false;
};

std::size_t queued_bytes(const Client& c) {
    return c.output.size() - c.sent;
}

// Preserve the bytes already written and cap the unsent output.
void queue_echo(Client& c, const char* payload, std::uint32_t length) {
    const std::size_t needed = kHeaderBytes + length;
    if (queued_bytes(c) + needed > kMaxOutput) {
        throw ProtocolError("output buffer limit exceeded");
    }
    // Reclaim the sent prefix before appending more frames.
    if (c.sent != 0) {
        c.output.erase(0, c.sent);
        c.sent = 0;
    }
    const std::uint32_t net_length = htonl(length);
    c.output.append(reinterpret_cast<const char*>(&net_length), kHeaderBytes);
    c.output.append(payload, length);
}

// A read may contain half a frame, exactly one frame, or many frames.
void parse_frames(Client& c) {
    while (c.input.size() >= kHeaderBytes) {
        std::uint32_t network_length = 0;
        std::memcpy(&network_length, c.input.data(), kHeaderBytes);
        const std::uint32_t length = ntohl(network_length);
        if (length > kMaxFrame) {
            throw ProtocolError("frame too large");
        }
        if (c.input.size() - kHeaderBytes < length) {
            return;  // half frame: wait for more bytes
        }
        queue_echo(c, c.input.data() + kHeaderBytes, length);
        c.input.erase(0, kHeaderBytes + length);
    }
}

void read_available(Client& c, std::size_t& eagain_reads,
                    std::size_t& pauses) {
    char buffer[4096];
    for (;;) {
        if (queued_bytes(c) >= kHighWater) {
            c.read_paused = true;
            ++pauses;
            return;
        }
        const ssize_t n = ::recv(c.fd.get(), buffer, sizeof(buffer), 0);
        if (n > 0) {
            c.input.append(buffer, static_cast<std::size_t>(n));
            if (c.input.size() > kMaxInput) {
                throw ProtocolError("input buffer limit exceeded");
            }
            parse_frames(c);
        } else if (n == 0) {
            c.read_eof = true;
            if (!c.input.empty()) {
                throw ProtocolError("EOF during incomplete frame");
            }
            return;
        } else if (errno == EINTR) {
            continue;
        } else if (errno == EAGAIN || errno == EWOULDBLOCK) {
            ++eagain_reads;
            return;
        } else {
            throw std::system_error(errno, std::generic_category(), "recv");
        }
    }
}

void write_available(Client& c) {
    while (c.sent < c.output.size()) {
        const ssize_t n = ::send(
            c.fd.get(), c.output.data() + c.sent,
            c.output.size() - c.sent, MSG_NOSIGNAL);
        if (n > 0) {
            c.sent += static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            break;
        } else {
            throw std::system_error(n == 0 ? EPIPE : errno,
                                    std::generic_category(), "send");
        }
    }
    if (c.sent == c.output.size()) {
        c.output.clear();
        c.sent = 0;
    }
    // Hysteresis: restart reads only once output drops below low water.
    if (c.read_paused && queued_bytes(c) <= kLowWater) {
        c.read_paused = false;
    }
}

void run(int port, int client_limit) {
    agent::UniqueFd listener(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    check(listener.valid(), "socket");
    const int reuse = 1;
    check(::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
                       &reuse, sizeof(reuse)) == 0, "setsockopt");
    make_nonblocking(listener.get());

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    check(::bind(listener.get(), reinterpret_cast<sockaddr*>(&addr),
                 sizeof(addr)) == 0, "bind");
    check(::listen(listener.get(), 64) == 0, "listen");
    socklen_t len = sizeof(addr);
    check(::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr),
                        &len) == 0, "getsockname");

    agent::UniqueFd epfd(::epoll_create1(EPOLL_CLOEXEC));
    check(epfd.valid(), "epoll_create1");
    watch(epfd.get(), EPOLL_CTL_ADD, listener.get(), EPOLLIN);

    std::unordered_map<int, Client> clients;
    int accepted = 0;
    int finished = 0;
    int rejected = 0;
    int failed = 0;
    std::size_t eagain_reads = 0;
    std::size_t pauses = 0;
    std::cout << "READY 127.0.0.1:" << ntohs(addr.sin_port) << std::endl;

    epoll_event events[64]{};
    while (finished < client_limit) {
        const int count = ::epoll_wait(epfd.get(), events, 64, -1);
        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "epoll_wait");
        }
        for (int i = 0; i < count; ++i) {
            const int fd = events[i].data.fd;
            if (fd == listener.get()) {
                if (accepted >= client_limit) continue;
                for (;;) {
                    const int raw = ::accept4(listener.get(), nullptr, nullptr,
                                              SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (raw < 0) {
                        if (errno == EINTR) continue;
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        throw std::system_error(errno, std::generic_category(), "accept4");
                    }
                    agent::UniqueFd peer(raw);
                    watch(epfd.get(), EPOLL_CTL_ADD, raw, EPOLLIN | EPOLLRDHUP);
                    clients.emplace(raw, Client{std::move(peer), {}, {}, 0, false, false});
                    if (++accepted == client_limit) {
                        check(::epoll_ctl(epfd.get(), EPOLL_CTL_DEL,
                                          listener.get(), nullptr) == 0,
                              "epoll_ctl DEL listener");
                        break;
                    }
                }
                continue;
            }

            auto it = clients.find(fd);
            if (it == clients.end()) continue;
            Client& c = it->second;
            bool error = (events[i].events & EPOLLERR) != 0;
            bool invalid_frame = false;
            if (!error) {
                try {
                    if (!c.read_eof && !c.read_paused &&
                        (events[i].events & (EPOLLIN | EPOLLRDHUP | EPOLLHUP))) {
                        read_available(c, eagain_reads, pauses);
                    }
                    if (queued_bytes(c) != 0) {
                        write_available(c);
                    }
                } catch (const ProtocolError& ex) {
                    std::cerr << "REJECTED " << fd << ": " << ex.what() << '\n';
                    invalid_frame = true;
                } catch (const std::exception& ex) {
                    std::cerr << "CLIENT_ERROR " << fd << ": " << ex.what() << '\n';
                    error = true;
                }
            }

            if (error || invalid_frame ||
                (c.read_eof && queued_bytes(c) == 0)) {
                check(::epoll_ctl(epfd.get(), EPOLL_CTL_DEL, fd, nullptr) == 0,
                      "epoll_ctl DEL client");
                clients.erase(it);  // UniqueFd closes exactly once
                ++finished;
                if (error) ++failed;
                if (invalid_frame) ++rejected;
                std::cout << "CLOSED " << finished << " ACTIVE "
                          << clients.size() << std::endl;
            } else {
                std::uint32_t wanted = 0;
                if (!c.read_eof && !c.read_paused) {
                    wanted |= EPOLLIN | EPOLLRDHUP;
                }
                if (queued_bytes(c) != 0) {
                    wanted |= EPOLLOUT;
                }
                watch(epfd.get(), EPOLL_CTL_MOD, fd, wanted);
            }
        }
    }
    std::cout << "DONE clients=" << finished << " rejected=" << rejected
              << " failed=" << failed << " pauses=" << pauses
              << " eagain_reads=" << eagain_reads
              << " active=" << clients.size() << std::endl;
    if (failed != 0) throw std::runtime_error("client I/O errors occurred");
}

} // namespace

int main(int argc, char** argv) {
    try {
        const int port = argc > 1 ? std::stoi(argv[1]) : 9092;
        const int limit = argc > 2 ? std::stoi(argv[2]) : 1;
        if (port < 0 || port > 65535 || limit <= 0) {
            throw std::invalid_argument("invalid port or client limit");
        }
        run(port, limit);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "framed server error: " << ex.what() << '\n';
        return 1;
    }
}
