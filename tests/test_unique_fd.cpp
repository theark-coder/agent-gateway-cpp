
#include "agent/unique_fd.hpp"

#include <cerrno>
#include <fcntl.h>
#include <iostream>
#include <utility>
#include <unistd.h>

namespace {

bool is_closed(int fd) {
    errno = 0;
    return ::fcntl(fd, F_GETFD) == -1 && errno == EBADF;
}

// 测试 1：正常离开作用域时自动关闭
bool test_scope_close() {
    int fds[2];

    if (::pipe(fds) != 0) {
        return false;
    }

    const int read_fd = fds[0];
    const int write_fd = fds[1];

    {
        agent::UniqueFd read_end(read_fd);
        agent::UniqueFd write_end(write_fd);

        if (!read_end.valid() || !write_end.valid()) {
            return false;
        }
    }

    return is_closed(read_fd) && is_closed(write_fd);
}

// 测试 2：移动后，只有目标对象拥有资源
bool test_move_ownership() {
    int fds[2];

    if (::pipe(fds) != 0) {
        return false;
    }

    const int read_fd = fds[0];
    const int write_fd = fds[1];
    bool ownership_ok = false;

    {
        agent::UniqueFd source(read_fd);
        agent::UniqueFd write_end(write_fd);

        agent::UniqueFd target(std::move(source));

        ownership_ok =
            !source.valid() &&
            target.valid() &&
            target.get() == read_fd;
    }

    return ownership_ok &&
           is_closed(read_fd) &&
           is_closed(write_fd);
}

// 创建资源后提前返回，验证析构函数仍会执行
int create_fd_then_return() {
    int fds[2];

    if (::pipe(fds) != 0) {
        return -1;
    }

    agent::UniqueFd read_end(fds[0]);
    agent::UniqueFd write_end(fds[1]);

    return read_end.get();
}

bool test_early_return() {
    const int fd = create_fd_then_return();

    return fd >= 0 && is_closed(fd);
}

}  // namespace

int main() {
    const bool scope_ok = test_scope_close();
    const bool move_ok = test_move_ownership();
    const bool early_return_ok = test_early_return();

    std::cout << "["
              << (scope_ok ? "PASS" : "FAIL")
              << "] scope close\n";

    std::cout << "["
              << (move_ok ? "PASS" : "FAIL")
              << "] move ownership\n";

    std::cout << "["
              << (early_return_ok ? "PASS" : "FAIL")
              << "] early return cleanup\n";

    return scope_ok && move_ok && early_return_ok ? 0 : 1;
}
