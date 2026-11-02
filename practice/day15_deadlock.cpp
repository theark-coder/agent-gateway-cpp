
#include <iostream>
#include <mutex>
#include <thread>

int main() {
    std::mutex mutex_a;
    std::mutex mutex_b;

    std::thread t1([&]() {
        std::cout << "Thread 1 requesting both locks\n";

        {
            std::scoped_lock lock(mutex_a, mutex_b);
            std::cout << "Thread 1 acquired both locks\n";
        }

        std::cout << "Thread 1 finished\n";
    });

    std::thread t2([&]() {
        std::cout << "Thread 2 requesting both locks\n";

        {
            // 故意反转参数顺序
            std::scoped_lock lock(mutex_b, mutex_a);
            std::cout << "Thread 2 acquired both locks\n";
        }

        std::cout << "Thread 2 finished\n";
    });

    t1.join();
    t2.join();

    std::cout << "All threads completed\n";

    return 0;
}
