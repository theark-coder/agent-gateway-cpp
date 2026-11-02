#include <cerrno>
#include <cstring>
#include <iostream>
#include <string>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

// 处理可能发生的短写和 EINTR
bool write_all(int fd, const char* data, std::size_t size) {
    while (size > 0) {
        ssize_t n = write(fd, data, size);

        if (n > 0) {
            data += n;
            size -= static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            return false;
        }
    }

    return true;
}

int main() {
    int pipefd[2];

    // 创建管道
    if (pipe(pipefd) == -1) {
        perror("pipe");
        return 1;
    }

    // 创建子进程
    pid_t pid = fork();

    if (pid == -1) {
        perror("fork");
        close(pipefd[0]);
        close(pipefd[1]);
        return 1;
    }

    if (pid == 0) {
        // 子进程：只负责读取
        close(pipefd[1]);

        char buffer[128];
        std::string received;

        while (true) {
            ssize_t n = read(
                pipefd[0],
                buffer,
                sizeof(buffer)
            );

            if (n > 0) {
                received.append(
                    buffer,
                    static_cast<std::size_t>(n)
                );
            } else if (n == 0) {
                break;
            } else if (errno == EINTR) {
                continue;
            } else {
                perror("read");
                close(pipefd[0]);
                _exit(2);
            }
        }

        close(pipefd[0]);

        std::cout << "Child received: "
                  << received << std::endl;

        _exit(0);
    }

    // 父进程：只负责写入
    close(pipefd[0]);

    const std::string message =
        "Hello from parent process!";

    bool write_ok = write_all(
        pipefd[1],
        message.data(),
        message.size()
    );

    if (!write_ok) {
        perror("write");
    }

    // 关闭写端，向子进程传递 EOF
    close(pipefd[1]);

    // 等待子进程退出
    int status = 0;

    while (waitpid(pid, &status, 0) == -1) {
        if (errno != EINTR) {
            perror("waitpid");
            return 1;
        }
    }

    if (!write_ok) {
        return 1;
    }

    if (WIFEXITED(status)) {
        return WEXITSTATUS(status);
    }

    return 1;
}