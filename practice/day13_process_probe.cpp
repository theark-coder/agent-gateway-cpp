
#include <atomic>
#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

#include <unistd.h>

int main() {
    std::atomic<bool> stop{false};

    std::vector<std::thread> workers;

    // 创建三个工作线程
    for (int i = 0; i < 3; ++i) {
        workers.emplace_back([&stop]() {
            while (!stop.load()) {
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(100)
                );
            }
        });
    }

    std::cout << "Process PID: " << getpid()
              << std::endl;

    std::cout << "Worker threads: "
              << workers.size() << std::endl;

    // 保持进程运行5分钟，供Linux命令检查
    std::this_thread::sleep_for(
        std::chrono::minutes(5)
    );

    stop.store(true);

    for (auto& worker : workers) {
        worker.join();
    }

    return 0;
}
