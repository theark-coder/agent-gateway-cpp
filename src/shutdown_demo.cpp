
#include "agent/thread_pool.hpp"

#include <atomic>
#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <ctime>
#include <cstring>
#include <fcntl.h>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>

#include <pthread.h>
#include <unistd.h>

// 保证 write 完整写入
bool write_all(int fd, const char* data, std::size_t size) {
    while (size > 0) {
        ssize_t n = ::write(fd, data, size);

        if (n > 0) {
            data += n;
            size -= static_cast<std::size_t>(n);
        } else if (n < 0 && errno == EINTR) {
            continue;
        } else {
            if (n == 0) {
                errno = EIO;
            }
            return false;
        }
    }

    return true;
}

// 追加一条 JSON 日志
bool append_json_event(
    const char* event,
    const char* signal_name
) {
    int fd = ::open(
        "logs/day14_events.jsonl",
        O_WRONLY | O_CREAT | O_APPEND | O_CLOEXEC,
        0644
    );

    if (fd == -1) {
        perror("open log");
        return false;
    }

    std::time_t now = std::time(nullptr);
    std::tm utc{};

    if (::gmtime_r(&now, &utc) == nullptr) {
        perror("gmtime_r");
        ::close(fd);
        return false;
    }

    char timestamp[32]{};

    if (std::strftime(
            timestamp,
            sizeof(timestamp),
            "%Y-%m-%dT%H:%M:%SZ",
            &utc
        ) == 0) {
        ::close(fd);
        return false;
    }

    char line[256]{};

    int length = std::snprintf(
        line,
        sizeof(line),
        "{\"event\":\"%s\",\"signal\":\"%s\","
        "\"timestamp\":\"%s\"}\n",
        event,
        signal_name,
        timestamp
    );

    if (length < 0 ||
        static_cast<std::size_t>(length) >= sizeof(line)) {
        ::close(fd);
        return false;
    }

    bool ok = write_all(
        fd,
        line,
        static_cast<std::size_t>(length)
    );

    if (!ok) {
        perror("write log");
    }

    // 每次写入后关闭 FD，避免泄漏
    if (::close(fd) == -1) {
        perror("close log");
        ok = false;
    }

    return ok;
}

int main() {

    // 1. 屏蔽 SIGTERM
    sigset_t signal_set;

    sigemptyset(&signal_set);
    sigaddset(&signal_set, SIGTERM);

    int rc = pthread_sigmask(
        SIG_BLOCK,
        &signal_set,
        nullptr
    );

    if (rc != 0) {
        std::cerr << "pthread_sigmask: "
                  << std::strerror(rc) << '\n';
        return 1;
    }

    // 2. 创建线程池
    agent::ThreadPool pool(3, 16);

    constexpr int task_count = 12;

    std::atomic<int> completed{0};
    std::mutex output_mutex;

    // 3. 提交12个模拟任务
    for (int i = 1; i <= task_count; ++i) {

        pool.submit([i, &completed, &output_mutex]() {

            std::this_thread::sleep_for(
                std::chrono::milliseconds(1000)
            );

            ++completed;

            std::lock_guard<std::mutex> lock(output_mutex);

            std::cout << "Task "
                      << i << " completed\n";
        });
    }

    // 4. 输出 PID，等待外部 SIGTERM
    const pid_t pid = ::getpid();

    std::cout << "READY PID=" << pid << std::endl;
    std::cout << "Waiting for SIGTERM..." << std::endl;

    int received_signal = 0;

    rc = sigwait(&signal_set, &received_signal);

    if (rc != 0) {
        std::cerr << "sigwait: "
                  << std::strerror(rc) << '\n';
        pool.shutdown();
        return 1;
    }

    const char* signal_name =
        received_signal == SIGTERM ? "SIGTERM" : "UNKNOWN";

    std::cout << "Received " << signal_name << '\n';

    // 5. 记录退出请求
    bool request_logged = append_json_event(
        "shutdown_requested",
        signal_name
    );

    // 6. 安全关闭线程池
    pool.shutdown();

    // 7. 检查剩余任务是否全部执行
    const int done = completed.load();

    std::cout << "Completed tasks: "
              << done << "/" << task_count << '\n';

    // 8. 验证关闭后不能提交新任务
    bool rejected = false;

    try {
        pool.submit([] {});
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    std::cout << "Submit after shutdown: "
              << (rejected ? "REJECTED" : "ACCEPTED")
              << '\n';

    // 9. 记录退出完成
    bool complete_logged = append_json_event(
        "shutdown_complete",
        signal_name
    );

    std::cout << "Shutdown complete." << std::endl;

    // 实验检查窗口：方便检查 /proc/PID/fd
    std::cout << "Inspection PID=" << pid << std::endl;

    std::this_thread::sleep_for(
        std::chrono::seconds(5)
    );

    if (!request_logged ||
        !complete_logged ||
        done != task_count ||
        !rejected) {
        return 1;
    }

    return 0;
}
