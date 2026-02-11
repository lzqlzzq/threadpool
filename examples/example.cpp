#include "../include/ThreadPool.hpp"
#include <iostream>
#include <chrono>
#include <mutex>

std::mutex print_mtx;

int main() {
    // Create a thread pool with 2 workers, NOT active yet
    thread_pool::ThreadPool pool(2, false);

    {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "=== Part 1: Priority Test ===" << std::endl;
        std::cout << "Submitting batch tasks with different priorities (pool not started)..." << std::endl;
    }

    // Submit tasks with different priorities while pool is inactive
    std::vector<thread_pool::ThreadPool::TaskHandle<int>> batch;
    for (int i = 0; i < 5; ++i) {
        batch.push_back(pool.enqueue(static_cast<double>(i), [i]() {
            {
                std::lock_guard<std::mutex> lk(print_mtx);
                std::cout << "[Batch " << i << "] priority=" << i << std::endl;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            return i;
        }));
    }

    // Now start the pool - higher priority tasks should run first
    {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "Starting pool... (expect: 4, 3, 2, 1, 0)" << std::endl;
    }
    pool.start();

    // Wait for batch to complete
    for (auto& h : batch) {
        h.future.wait();
    }

    {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "\n=== Part 2: Cancel Test ===" << std::endl;
    }

    // Submit blockers to occupy all workers
    auto blocker1 = pool.enqueue(100.0, []() {
        {
            std::lock_guard<std::mutex> lk(print_mtx);
            std::cout << "[Blocker 1] Running..." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        {
            std::lock_guard<std::mutex> lk(print_mtx);
            std::cout << "[Blocker 1] Done!" << std::endl;
        }
    });

    auto blocker2 = pool.enqueue(100.0, []() {
        {
            std::lock_guard<std::mutex> lk(print_mtx);
            std::cout << "[Blocker 2] Running..." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        {
            std::lock_guard<std::mutex> lk(print_mtx);
            std::cout << "[Blocker 2] Done!" << std::endl;
        }
    });

    // Wait for blockers to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Submit a task to cancel
    auto cancelMe = pool.enqueue(1.0, []() {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "[CancelMe] ERROR: This should NOT print!" << std::endl;
        return 999;
    });

    bool cancelled = cancelMe.cancel();
    {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "Cancel result: " << (cancelled ? "SUCCESS" : "FAILED") << std::endl;
        std::cout << "Task state: " << (cancelMe.get_state() == thread_pool::ThreadPool::Task::Cancel ? "Cancelled" : "Other") << std::endl;
    }

    // Wait for blockers
    blocker1.future.wait();
    blocker2.future.wait();

    {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "\n=== Part 3: Set Priority Test ===" << std::endl;
    }

    // Submit new blockers
    auto blocker3 = pool.enqueue(100.0, []() {
        {
            std::lock_guard<std::mutex> lk(print_mtx);
            std::cout << "[Blocker 3] Running..." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        {
            std::lock_guard<std::mutex> lk(print_mtx);
            std::cout << "[Blocker 3] Done!" << std::endl;
        }
    });

    auto blocker4 = pool.enqueue(100.0, []() {
        {
            std::lock_guard<std::mutex> lk(print_mtx);
            std::cout << "[Blocker 4] Running..." << std::endl;
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
        {
            std::lock_guard<std::mutex> lk(print_mtx);
            std::cout << "[Blocker 4] Done!" << std::endl;
        }
    });

    // Wait for blockers to start
    std::this_thread::sleep_for(std::chrono::milliseconds(50));

    // Submit low priority task, then boost its priority
    auto lowTask = pool.enqueue(1.0, []() {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "[LowTask] Running (was priority 1.0, boosted to 50.0)" << std::endl;
        return 42;
    });

    auto highTask = pool.enqueue(10.0, []() {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "[HighTask] Running (priority 10.0)" << std::endl;
        return 100;
    });

    // Boost lowTask priority above highTask
    bool boosted = lowTask.set_priority(50.0);
    {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "Priority boost result: " << (boosted ? "SUCCESS" : "FAILED") << std::endl;
        std::cout << "Expect: LowTask runs before HighTask" << std::endl;
    }

    // Wait for all
    blocker3.future.wait();
    blocker4.future.wait();
    lowTask.future.wait();
    highTask.future.wait();

    {
        std::lock_guard<std::mutex> lk(print_mtx);
        std::cout << "\n=== Done ===" << std::endl;
    }

    return 0;
}
