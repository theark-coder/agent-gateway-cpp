
#include "agent/task.hpp"
#include "agent/thread_pool.hpp"
#include <cstring>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {

bool transition(
    agent::TaskService& service,
    int id,
    agent::TaskStatus from,
    agent::TaskStatus to
) {
    return service.transition_task(id, from, to);
}

void update_peak(std::atomic<int>& peak, int current) {
    int old = peak.load();

    while (current > old &&
           !peak.compare_exchange_weak(old, current)) {
    }
}

// 场景1：正常执行，验证任务ID与结果对应
bool test_normal_execution() {
    agent::TaskService service;
    agent::ThreadPool pool(3, 16);

    std::vector<int> ids;
    std::vector<std::future<int>> futures;

    for (int i = 0; i < 8; ++i) {
        auto created = service.create_task("normal task");

        if (!created.success) return false;

        int id = created.task_id;
        ids.push_back(id);

        futures.emplace_back(pool.submit([&, id, i] {
            if (!transition(
                    service, id,
                    agent::TaskStatus::pending,
                    agent::TaskStatus::running)) {
                throw std::runtime_error("pending -> running failed");
            }

            std::this_thread::sleep_for(2ms);

            if (!transition(
                    service, id,
                    agent::TaskStatus::running,
                    agent::TaskStatus::completed)) {
                throw std::runtime_error("running -> completed failed");
            }

            return id * 100 + i;
        }));
    }

    bool ok = true;

    for (std::size_t i = 0; i < futures.size(); ++i) {
        try {
            if (futures[i].get() != ids[i] * 100 +
                                      static_cast<int>(i)) {
                ok = false;
            }
        } catch (...) {
            ok = false;
        }
    }

    pool.shutdown();

    for (int id : ids) {
        auto task = service.find_task(id);

        if (!task || task->status != agent::TaskStatus::completed) {
            ok = false;
        }
    }

    const auto stats = pool.stats();

    return ok &&
           stats.submitted == 8 &&
           stats.completed == 8;
}

// 场景2：异常执行，一个任务失败不影响其他任务
bool test_exception_isolation() {
    agent::TaskService service;
    agent::ThreadPool pool(2, 4);

    auto bad = service.create_task("failing task");
    auto good = service.create_task("normal task");

    if (!bad.success || !good.success) return false;

    auto bad_future = pool.submit([&] {
        if (!transition(
                service, bad.task_id,
                agent::TaskStatus::pending,
                agent::TaskStatus::running)) {
            throw std::runtime_error("bad task start failed");
        }

        if (!transition(
                service, bad.task_id,
                agent::TaskStatus::running,
                agent::TaskStatus::failed)) {
            throw std::runtime_error("bad task failure transition failed");
        }

        throw std::runtime_error("simulated task failure");
        return 0;
    });

    auto good_future = pool.submit([&] {
        if (!transition(
                service, good.task_id,
                agent::TaskStatus::pending,
                agent::TaskStatus::running)) {
            throw std::runtime_error("good task start failed");
        }

        if (!transition(
                service, good.task_id,
                agent::TaskStatus::running,
                agent::TaskStatus::completed)) {
            throw std::runtime_error("good task completion failed");
        }

        return 42;
    });

    bool exception_caught = false;

    try {
        (void)bad_future.get();
    } catch (const std::runtime_error&) {
        exception_caught = true;
    }

    bool good_ok = false;

    try {
        good_ok = good_future.get() == 42;
    } catch (...) {
        good_ok = false;
    }

    pool.shutdown();

    auto bad_task = service.find_task(bad.task_id);
    auto good_task = service.find_task(good.task_id);
    auto stats = pool.stats();

    return exception_caught &&
           good_ok &&
           bad_task &&
           bad_task->status == agent::TaskStatus::failed &&
           good_task &&
           good_task->status == agent::TaskStatus::completed &&
           stats.submitted == 2 &&
           stats.completed == 2;
}

// 场景3：队列满，验证受控拒绝
bool test_queue_full() {
    agent::TaskService service;
    agent::ThreadPool pool(1, 1);

    auto first = service.create_task("first task");
    auto second = service.create_task("second task");
    auto third = service.create_task("rejected task");

    if (!first.success || !second.success || !third.success) {
        return false;
    }

    std::promise<void> started_promise;
    auto started = started_promise.get_future();

    std::promise<void> release_promise;
    auto release = release_promise.get_future().share();

    auto first_future = pool.submit([&] {
        if (!transition(
                service, first.task_id,
                agent::TaskStatus::pending,
                agent::TaskStatus::running)) {
            throw std::runtime_error("first start failed");
        }

        started_promise.set_value();
        release.wait();

        if (!transition(
                service, first.task_id,
                agent::TaskStatus::running,
                agent::TaskStatus::completed)) {
            throw std::runtime_error("first completion failed");
        }

        return 1;
    });

    started.wait();

    // 工作线程被第一个任务占用，队列容量为1
    auto second_future = pool.submit([&] {
        if (!transition(
                service, second.task_id,
                agent::TaskStatus::pending,
                agent::TaskStatus::running)) {
            throw std::runtime_error("second start failed");
        }

        if (!transition(
                service, second.task_id,
                agent::TaskStatus::running,
                agent::TaskStatus::completed)) {
            throw std::runtime_error("second completion failed");
        }

        return 2;
    });

    bool rejected = false;

    try {
        auto third_future = pool.submit([] {
            return 3;
        });
        (void)third_future;
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    bool state_ok = transition(
        service,
        third.task_id,
        agent::TaskStatus::pending,
        agent::TaskStatus::rejected
    );

    // 无论前面的检查如何，都释放工作线程
    release_promise.set_value();

    bool results_ok = false;

    try {
        results_ok =
            first_future.get() == 1 &&
            second_future.get() == 2;
    } catch (...) {
        results_ok = false;
    }

    pool.shutdown();

    auto stats = pool.stats();
    auto rejected_task = service.find_task(third.task_id);

    return rejected &&
           state_ok &&
           results_ok &&
           rejected_task &&
           rejected_task->status == agent::TaskStatus::rejected &&
           stats.submitted == 2 &&
           stats.completed == 2;
}

// 场景4：协作式取消
bool test_cooperative_cancellation() {
    agent::TaskService service;
    agent::ThreadPool pool(1, 4);

    auto created = service.create_task("cancellable task");

    if (!created.success) return false;

    std::promise<void> started_promise;
    auto started = started_promise.get_future();

    auto task = pool.submit_cancellable(
        [&](agent::CancellationToken token) {
            if (!transition(
                    service, created.task_id,
                    agent::TaskStatus::pending,
                    agent::TaskStatus::running)) {
                throw std::runtime_error("cancel task start failed");
            }

            started_promise.set_value();

            while (!token.stop_requested()) {
                std::this_thread::sleep_for(1ms);
            }

            if (!transition(
                    service, created.task_id,
                    agent::TaskStatus::running,
                    agent::TaskStatus::cancelled)) {
                throw std::runtime_error("cancel transition failed");
            }

            return created.task_id;
        }
    );

    started.wait();

    task.cancellation.request_cancel();

    bool result_ok = false;

    try {
        result_ok = task.future.get() == created.task_id;
    } catch (...) {
        result_ok = false;
    }

    pool.shutdown();

    auto saved = service.find_task(created.task_id);
    auto stats = pool.stats();

    return result_ok &&
           saved &&
           saved->status == agent::TaskStatus::cancelled &&
           stats.submitted == 1 &&
           stats.completed == 1;
}

// 场景5：服务关闭、排空任务、限制并发
bool test_shutdown_and_thread_limit() {
    agent::TaskService service;
    agent::ThreadPool pool(2, 16);

    constexpr int task_count = 10;

    std::atomic<int> active{0};
    std::atomic<int> peak{0};

    std::vector<int> ids;
    std::vector<std::future<int>> futures;

    for (int i = 0; i < task_count; ++i) {
        auto created = service.create_task("shutdown task");

        if (!created.success) return false;

        int id = created.task_id;
        ids.push_back(id);

        futures.emplace_back(pool.submit([&, id] {
            if (!transition(
                    service, id,
                    agent::TaskStatus::pending,
                    agent::TaskStatus::running)) {
                throw std::runtime_error("shutdown task start failed");
            }

            int current = active.fetch_add(1) + 1;
            update_peak(peak, current);

            std::this_thread::sleep_for(5ms);

            active.fetch_sub(1);

            if (!transition(
                    service, id,
                    agent::TaskStatus::running,
                    agent::TaskStatus::completed)) {
                throw std::runtime_error("shutdown task completion failed");
            }

            return id;
        }));
    }

    // close 队列并等待全部已接纳任务执行完成
    pool.shutdown();

    bool results_ok = true;

    for (std::size_t i = 0; i < futures.size(); ++i) {
        try {
            if (futures[i].get() != ids[i]) {
                results_ok = false;
            }
        } catch (...) {
            results_ok = false;
        }
    }

    // 关闭后拒绝新任务
    auto after_shutdown = service.create_task("after shutdown");

    bool rejected = false;

    try {
        auto future = pool.submit([] {
            return 100;
        });
        (void)future;
    } catch (const std::runtime_error&) {
        rejected = true;
    }

    bool rejected_state_ok = transition(
        service,
        after_shutdown.task_id,
        agent::TaskStatus::pending,
        agent::TaskStatus::rejected
    );

    // 重复关闭也应安全
    pool.shutdown();

    bool states_ok = true;

    for (int id : ids) {
        auto task = service.find_task(id);

        if (!task || task->status != agent::TaskStatus::completed) {
            states_ok = false;
        }
    }

    auto stats = pool.stats();

    return results_ok &&
           states_ok &&
           rejected &&
           rejected_state_ok &&
           active.load() == 0 &&
           peak.load() <= 2 &&
           stats.submitted == task_count &&
           stats.completed == task_count;
}

} // namespace

int main(int argc, char* argv[]) {
    struct TestCase {
        const char* name;
        bool (*run)();
        const char* description;
    };

    const TestCase tests[] = {
        {
            "normal",
            test_normal_execution,
            "normal execution"
        },
        {
            "exception",
            test_exception_isolation,
            "exception isolation"
        },
        {
            "queue-full",
            test_queue_full,
            "queue full"
        },
        {
            "cancellation",
            test_cooperative_cancellation,
            "cooperative cancellation"
        },
        {
            "shutdown",
            test_shutdown_and_thread_limit,
            "shutdown and thread limit"
        }
    };

    const char* mode = argc > 1 ? argv[1] : "all";

    if (std::strcmp(mode, "all") == 0) {
        for (const auto& test : tests) {
            if (!test.run()) {
                std::cerr << "FAIL: "
                          << test.description << '\n';
                return 1;
            }

            std::cout << test.description
                      << ": PASS\n";
        }

        std::cout << "All Day16 integration tests passed\n";
        return 0;
    }

    for (const auto& test : tests) {
        if (std::strcmp(mode, test.name) == 0) {
            const bool passed = test.run();

            std::cout << test.description
                      << ": "
                      << (passed ? "PASS" : "FAIL")
                      << '\n';

            return passed ? 0 : 1;
        }
    }

    std::cerr
        << "Usage: " << argv[0]
        << " [all|normal|exception|queue-full|cancellation|shutdown]\n";

    return 2;
}