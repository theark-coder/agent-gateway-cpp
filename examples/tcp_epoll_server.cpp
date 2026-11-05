#include "agent/unique_fd.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstddef>
#include <cstdint>
#include <fcntl.h>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unordered_map>
#include <unistd.h>

namespace {
constexpr std::size_t kMaxPending = 1024 * 1024;

void check_syscall(bool ok, const char* operation) {
    if (!ok) throw std::system_error(errno, std::generic_category(), operation);
}

void nonblocking(int fd) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    check_syscall(flags >= 0, "fcntl F_GETFL");
    check_syscall(::fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0, "fcntl F_SETFL");
}

void watch(int epfd, int operation, int fd, std::uint32_t flags) {
    epoll_event ev{};
    ev.events = flags;
    ev.data.fd = fd;
    check_syscall(::epoll_ctl(epfd, operation, fd, &ev) == 0, "epoll_ctl");
}

struct Client {
    agent::UniqueFd fd;
    std::string pending;
    std::size_t sent = 0;
    bool eof = false;
};

void read_available(Client& client, std::size_t& eagain_reads) {
    char buffer[4096];
    for (;;) {
        const ssize_t n = ::recv(client.fd.get(), buffer, sizeof(buffer), 0);
        if (n > 0) {
            client.pending.append(buffer, static_cast<std::size_t>(n));
            if (client.pending.size() - client.sent > kMaxPending) {
                throw std::runtime_error("client output buffer limit exceeded");
            }
        } else if (n == 0) {
            client.eof = true;
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

void write_available(Client& client) {
    while (client.sent < client.pending.size()) {
        const ssize_t n = ::send(
            client.fd.get(),
            client.pending.data() + client.sent,
            client.pending.size() - client.sent,
            MSG_NOSIGNAL
        );
        if (n > 0) {
            client.sent += static_cast<std::size_t>(n);
        } else if (n == -1 && errno == EINTR) {
            continue;
        } else if (n == -1 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            return;
        } else {
            throw std::system_error(n == 0 ? EPIPE : errno,
                                    std::generic_category(), "send");
        }
    }
    client.pending.clear();
    client.sent = 0;
}

void run(int port, int limit) {
    agent::UniqueFd listener(::socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0));
    check_syscall(listener.valid(), "socket");
    const int reuse = 1;
    check_syscall(::setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR,
                               &reuse, sizeof(reuse)) == 0, "setsockopt");
    nonblocking(listener.get());

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(static_cast<std::uint16_t>(port));
    check_syscall(::bind(listener.get(), reinterpret_cast<sockaddr*>(&addr),
                         sizeof(addr)) == 0, "bind");
    check_syscall(::listen(listener.get(), 64) == 0, "listen");
    socklen_t address_length = sizeof(addr);
    check_syscall(::getsockname(listener.get(), reinterpret_cast<sockaddr*>(&addr),
                                &address_length) == 0, "getsockname");

    agent::UniqueFd epfd(::epoll_create1(EPOLL_CLOEXEC));
    check_syscall(epfd.valid(), "epoll_create1");
    watch(epfd.get(), EPOLL_CTL_ADD, listener.get(), EPOLLIN);

    std::unordered_map<int, Client> clients;
    int accepted = 0;
    int finished = 0;
    int failed = 0;
    std::size_t eagain_reads = 0;
    std::cout << "READY 127.0.0.1:" << ntohs(addr.sin_port) << std::endl;

    epoll_event events[64]{};
    while (finished < limit) {
        const int count = ::epoll_wait(epfd.get(), events, 64, -1);
        if (count < 0) {
            if (errno == EINTR) continue;
            throw std::system_error(errno, std::generic_category(), "epoll_wait");
        }
        for (int i = 0; i < count; ++i) {
            const int fd = events[i].data.fd;
            if (fd == listener.get()) {
                if (accepted >= limit) continue;
                for (;;) {
                    int raw = ::accept4(listener.get(), nullptr, nullptr,
                                        SOCK_NONBLOCK | SOCK_CLOEXEC);
                    if (raw < 0) {
                        if (errno == EINTR) continue;
                        if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                        throw std::system_error(errno, std::generic_category(), "accept4");
                    }
                    agent::UniqueFd peer(raw);
                    watch(epfd.get(), EPOLL_CTL_ADD, raw, EPOLLIN | EPOLLRDHUP);
                    clients.emplace(raw, Client{std::move(peer), {}, 0, false});
                    ++accepted;
                    if (accepted == limit) {
                        check_syscall(::epoll_ctl(epfd.get(), EPOLL_CTL_DEL,
                                                   listener.get(), nullptr) == 0,
                                      "epoll_ctl DEL listener");
                        break;
                    }
                }
                continue;
            }

            auto it = clients.find(fd);
            if (it == clients.end()) continue;
            Client& client = it->second;
            bool error = (events[i].events & EPOLLERR) != 0;
            if (!error) {
                try {
                    if (!client.eof && (events[i].events &
                        (EPOLLIN | EPOLLRDHUP | EPOLLHUP))) {
                        read_available(client, eagain_reads);
                    }
                    if (!client.pending.empty()) write_available(client);
                } catch (const std::exception& ex) {
                    std::cerr << "Client " << fd << ": " << ex.what() << '\n';
                    error = true;
                }
            }
            if (error || (client.eof && client.pending.empty())) {
                check_syscall(::epoll_ctl(epfd.get(), EPOLL_CTL_DEL,
                                           fd, nullptr) == 0,
                              "epoll_ctl DEL client");
                clients.erase(it); // UniqueFd destructor closes the fd
                ++finished;
                if (error) ++failed;
                std::cout << "CLOSED " << finished
                          << " ACTIVE " << clients.size() << std::endl;
            } else {
                std::uint32_t wanted = client.eof ? 0u : (EPOLLIN | EPOLLRDHUP);
                if (!client.pending.empty()) wanted |= EPOLLOUT;
                watch(epfd.get(), EPOLL_CTL_MOD, fd, wanted);
            }
        }
    }
    std::cout << "DONE clients=" << finished
              << " failed=" << failed
              << " eagain_reads=" << eagain_reads
              << " active=" << clients.size() << std::endl;
    if (failed != 0) throw std::runtime_error("client errors occurred");
}
} // namespace

int main(int argc, char** argv) {
    try {
        const int port = argc > 1 ? std::stoi(argv[1]) : 9091;
        const int max_clients = argc > 2 ? std::stoi(argv[2]) : 50;
        if (port < 0 || port > 65535 || max_clients <= 0) {
            throw std::invalid_argument("invalid port or client limit");
        }
        run(port, max_clients);
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "epoll server error: " << ex.what() << '\n';
        return 1;
    }
}
