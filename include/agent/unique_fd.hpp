
#pragma once

#include <unistd.h>
#include <utility>

namespace agent {

class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) noexcept
        : fd_(fd) {}

    ~UniqueFd() noexcept {
        reset();
    }

    // 文件描述符不能被复制，否则可能重复关闭
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    // 允许转移所有权
    UniqueFd(UniqueFd&& other) noexcept
        : fd_(std::exchange(other.fd_, -1)) {}

    UniqueFd& operator=(UniqueFd&& other) noexcept {
        if (this != &other) {
            reset(std::exchange(other.fd_, -1));
        }
        return *this;
    }

    int get() const noexcept {
        return fd_;
    }

    bool valid() const noexcept {
        return fd_ >= 0;
    }

    // 放弃所有权，由调用者负责关闭返回的 fd
    int release() noexcept {
        return std::exchange(fd_, -1);
    }

    // 关闭当前 fd，并接管 new_fd
    void reset(int new_fd = -1) noexcept {
        if (new_fd == fd_) {
            return;
        }

        int old_fd = std::exchange(fd_, new_fd);

        if (old_fd >= 0) {
            (void)::close(old_fd);
        }
    }

private:
    int fd_ = -1;
};

}  // namespace agent
