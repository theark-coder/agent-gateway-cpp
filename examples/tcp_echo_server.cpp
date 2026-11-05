
#include "agent/tcp_io.hpp"
#include "agent/unique_fd.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <system_error>
#include <sys/socket.h>

namespace {

int parse_port(int argc, char** argv) {
    const int port = argc > 1 ? std::stoi(argv[1]) : 9090;

    if (port < 0 || port > 65535) {
        throw std::invalid_argument(
            "port must be in [0, 65535]"
        );
    }

    return port;
}

void serve_one(int port) {
    agent::UniqueFd listener(
        ::socket(AF_INET, SOCK_STREAM, 0)
    );

    if (!listener.valid()) {
        throw std::system_error(
            errno, std::generic_category(), "socket"
        );
    }

    const int reuse = 1;

    if (::setsockopt(
            listener.get(), SOL_SOCKET, SO_REUSEADDR,
            &reuse, sizeof(reuse)) < 0) {
        throw std::system_error(
            errno, std::generic_category(), "setsockopt"
        );
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = htons(
        static_cast<std::uint16_t>(port)
    );

    if (::bind(
            listener.get(),
            reinterpret_cast<sockaddr*>(&addr),
            sizeof(addr)) < 0) {
        throw std::system_error(
            errno, std::generic_category(), "bind"
        );
    }

    if (::listen(listener.get(), 8) < 0) {
        throw std::system_error(
            errno, std::generic_category(), "listen"
        );
    }

    // 当 port=0 时，由系统自动分配端口
    socklen_t addr_len = sizeof(addr);

    if (::getsockname(
            listener.get(),
            reinterpret_cast<sockaddr*>(&addr),
            &addr_len) < 0) {
        throw std::system_error(
            errno, std::generic_category(), "getsockname"
        );
    }

    std::cout << "READY 127.0.0.1:"
              << ntohs(addr.sin_port) << std::endl;

    int accepted = -1;

    do {
        accepted = ::accept(
            listener.get(), nullptr, nullptr
        );
    } while (accepted < 0 && errno == EINTR);

    if (accepted < 0) {
        throw std::system_error(
            errno, std::generic_category(), "accept"
        );
    }

    agent::UniqueFd peer(accepted);

    char buffer[4096];
    std::size_t total = 0;

    for (;;) {
        const ssize_t n = ::recv(
            peer.get(), buffer, sizeof(buffer), 0
        );

        if (n > 0) {
            agent::send_all(
                peer.get(),
                std::string_view(
                    buffer,
                    static_cast<std::size_t>(n)
                )
            );

            total += static_cast<std::size_t>(n);
        } else if (n == 0) {
            // 对端关闭发送方向
            break;
        } else if (errno != EINTR) {
            throw std::system_error(
                errno, std::generic_category(), "recv"
            );
        }
    }

    std::cout << "ECHO_OK bytes="
              << total << '\n';
}

} // namespace

int main(int argc, char** argv) {
    try {
        serve_one(parse_port(argc, argv));
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Server error: "
                  << ex.what() << '\n';
        return 1;
    }
}
