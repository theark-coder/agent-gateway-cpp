
#include "agent/thread_pool.hpp"

#include <chrono>
#include <exception>
#include <future>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

class TaskLogger {
public:
    void log(
        int task_id,
        const std::string& event,
        const std::string& detail = ""
    ) {
        std::lock_guard<std::mutex> lock(mutex_);

        std::clog
            << "task_id=" << task_id
            << " event=" << event
            << " thread_id=" << std::this_thread::get_id();

        if (!detail.empty()) {
            std::clog << " detail=" << detail;
        }

        std::clog << '\n';
    }

private:
    std::mutex mutex_;
};

int main() {
    agent::ThreadPool pool(3, 16);

    TaskLogger logger;
    std::vector<std::future<void>> futures;

    for (int id = 101; id <= 105; ++id) {
        futures.emplace_back(
            pool.submit([id, &logger]() {

                logger.log(id, "started");

                try {
                    std::this_thread::sleep_for(
                        std::chrono::milliseconds(100)
                    );

                    if (id == 103) {
                        throw std::runtime_error(
                            "simulated task failure"
                        );
                    }

                    logger.log(id, "completed");

                } catch (const std::exception& e) {
                    logger.log(id, "failed", e.what());
                    throw;
                }
            })
        );
    }

    int succeeded = 0;
    int failed = 0;

    for (auto& future : futures) {
        try {
            future.get();
            ++succeeded;
        } catch (const std::exception&) {
            ++failed;
        }
    }

    pool.shutdown();

    std::cout
        << "Task results: completed=" << succeeded
        << " failed=" << failed << '\n';

    return (succeeded == 4 && failed == 1) ? 0 : 1;
}
