
#include <functional>
#include <iostream>
#include <memory>
#include <utility>

namespace {

int failures = 0;

void check(bool passed, const char* name) {
    std::cout << '[' << (passed ? "PASS" : "FAIL")
              << "] " << name << '\n';

    if (!passed) {
        ++failures;
    }
}

struct Resource {
    bool* destroyed;

    explicit Resource(bool* state) : destroyed(state) {}

    ~Resource() {
        *destroyed = true;
    }
};

struct Task {
    int id;
    std::function<void()> on_complete;

    inline static int destroyed_count = 0;

    explicit Task(int task_id) : id(task_id) {}

    ~Task() {
        ++destroyed_count;
    }

    void run_callback() {
        if (on_complete) {
            on_complete();
        }
    }
};

bool test_unique_ptr_move() {
    bool destroyed = false;

    {
        auto first = std::make_unique<Resource>(&destroyed);
        auto second = std::move(first);

        if (first || !second || destroyed) {
            return false;
        }

        second.reset();

        if (!destroyed) {
            return false;
        }
    }

    return destroyed;
}

bool test_shared_ptr_lifetime() {
    Task::destroyed_count = 0;

    {
        auto first = std::make_shared<Task>(7);
        auto second = first;

        if (first.use_count() != 2 ||
            second.use_count() != 2) {
            return false;
        }

        first.reset();

        if (Task::destroyed_count != 0 ||
            !second || second->id != 7) {
            return false;
        }

        second.reset();

        if (Task::destroyed_count != 1) {
            return false;
        }
    }

    return Task::destroyed_count == 1;
}

bool test_weak_ptr_callback() {
    Task::destroyed_count = 0;

    bool saw_live_task = false;
    bool saw_expired_task = false;

    std::function<void()> saved_callback;
    std::weak_ptr<Task> observer;

    {
        auto task = std::make_shared<Task>(42);
        observer = task;

        std::weak_ptr<Task> weak_task = task;

        task->on_complete = [
            weak_task,
            &saw_live_task,
            &saw_expired_task
        ]() {
            if (auto locked = weak_task.lock()) {
                saw_live_task = (locked->id == 42);
            } else {
                saw_expired_task = true;
            }
        };

        // 保存回调，模拟任务销毁后回调仍然存在
        saved_callback = task->on_complete;

        task->run_callback();

        // lock() 的临时强引用已经释放
        if (task.use_count() != 1) {
            return false;
        }
    }

    // Task 已销毁，但保存的回调仍然存在
    if (!observer.expired() ||
        Task::destroyed_count != 1) {
        return false;
    }

    saved_callback();

    return saw_live_task &&
           saw_expired_task &&
           Task::destroyed_count == 1;
}

}  // namespace

int main() {
    check(
        test_unique_ptr_move(),
        "unique_ptr transfers ownership"
    );

    check(
        test_shared_ptr_lifetime(),
        "shared_ptr keeps object alive"
    );

    check(
        test_weak_ptr_callback(),
        "weak_ptr callback handles expired task"
    );

    std::cout << "Failures: " << failures << '\n';

    return failures == 0 ? 0 : 1;
}
