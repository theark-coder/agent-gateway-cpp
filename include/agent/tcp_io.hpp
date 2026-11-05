
#pragma once

#include <cerrno>
#include <cstddef>
#include <stdexcept>
#include <string_view>
#include <system_error>
#include <sys/socket.h>

namespace agent {

// 发送全部数据，处理短写与 EINTR
inline void send_all(int fd, std::string_view data) {
    std::size_t offset = 0;

    int flags = 0;
#ifdef MSG_NOSIGNAL
    flags = MSG_NOSIGNAL;
#endif

    while (offset < data.size()) {
        const ssize_t n = ::send(
            fd,
            data.data() + offset,
            data.size() - offset,
            flags
        );

        if (n > 0) {
            offset += static_cast<std::size_t>(n);
        } else if (n == -1 && errno == EINTR) {
            continue;
        } else if (n == 0) {
            throw std::runtime_error(
                "send returned zero before completion"
            );
        } else {
            throw std::system_error(
                errno, std::generic_category(), "send"
            );
        }
    }
}

} // namespace agent
