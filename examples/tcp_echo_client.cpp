
#include "agent/tcp_io.hpp"
#include "agent/unique_fd.hpp"

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <system_error>
#include <sys/socket.h>

int main(int argc, char** argv) {
    try {
        const int port =
            argc > 1 ? std::stoi(argv[1]) : 9090;

        if (port < 1 || port > 65535) {
            throw std::invalid_argument(
                "client port must be in [1, 65535]"
            );
        }

        const std::string message =
            argc > 2 ? argv[2] : "Hello Agent Gateway";

        agent::UniqueFd connection(
            ::socket(AF_INET, SOCK_STREAM, 0)
        );

        if (!connection.valid()) {
            throw std::system_error(
                errno, std::generic_category(), "socket"
            );
        }

        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        addr.sin_port = htons(
            static_cast<std::uint16_t>(port)
        );

        if (::connect(
                connection.get(),
                reinterpret_cast<sockaddr*>(&addr),
                sizeof(addr)) < 0) {
            throw std::system_error(
                errno, std::generic_category(), "connect"
            );
        }

        agent::send_all(connection.get(), message);

        // 告诉服务端：请求数据发送结束
        // 但客户端仍可以继续接收数据
        if (::shutdown(connection.get(), SHUT_WR) < 0) {
            throw std::system_error(
                errno, std::generic_category(), "shutdown"
            );
        }

        std::string echoed;
        char buffer[4096];

        for (;;) {
            const ssize_t n = ::recv(
                connection.get(),
                buffer,
                sizeof(buffer),
                0
            );

            if (n > 0) {
                echoed.append(
                    buffer,
                    static_cast<std::size_t>(n)
                );
            } else if (n == 0) {
                break;
            } else if (errno != EINTR) {
                throw std::system_error(
                    errno, std::generic_category(), "recv"
                );
            }
        }

        if (echoed != message) {
            std::cerr
                << "FAIL: echoed message mismatch\n";
            return 1;
        }

        std::cout << "PASS: echo bytes="
                  << echoed.size() << '\n';

        if (echoed.size() <= 80) {
            std::cout << "echo: " << echoed << '\n';
        }

        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "Client error: "
                  << ex.what() << '\n';
        return 1;
    }
}
