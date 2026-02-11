#pragma once

#include <algorithm>
#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>
#include <thread>
#include <type_traits>
#include <utility>


namespace thread_pool {

#ifdef __cpp_lib_hardware_interference_size
    static constexpr std::size_t hardware_destructive_interference_size =
        std::hardware_destructive_interference_size;
#else
    static constexpr std::size_t hardware_destructive_interference_size = 64;
#endif
 
// Fuck you, committe
#if __cplusplus >= 201703L
template <class F, class... Args>
using result_of_t = std::invoke_result_t<F, Args...>;

template<class... Bs> using disjunction = std::disjunction<Bs...>;
template<class T> using decay_t = std::decay_t<T>;
template<bool B, class T = void> using enable_if_t = std::enable_if_t<B, T>;
#else
template <class F, class... Args>
using result_of_t = typename std::result_of<F(Args...)>::type;

template<class...> struct disjunction : std::false_type {};
template<class B> struct disjunction<B> : B {};
template<class B, class... Bs> struct disjunction<B, Bs...>
    : std::conditional<bool(B::value), B, disjunction<Bs...>>::type {};

template<class T> using decay_t = typename std::decay<T>::type;
template<bool B, class T = void> using enable_if_t = typename std::enable_if<B, T>::type;
#endif

class ThreadPool {
public:
    ThreadPool(size_t numWorkers, bool active = true) : numWorkers(numWorkers), stop_(false) {
        if(active) this->start();
    };

    class alignas(hardware_destructive_interference_size) Task {
    public:
        enum State {
            Queued,
            Running,
            Done,
            Cancel
        };

    private:
        Task(double priority, std::function<void()> func) :
            priority(priority), func(std::move(func)), state(Queued) {};

        double priority;

        std::function<void()> func;
        std::atomic<State> state;

        friend class ThreadPool;
    };

    template<typename ReturnT>
    class TaskHandle {
    public:
        Task::State get_state() const {
            return this->task->state.load(std::memory_order_acquire);
        };
        bool cancel() {
            Task::State expected = Task::State::Queued;
            return this->task->state.compare_exchange_strong(
                expected, Task::State::Cancel, std::memory_order_acq_rel);
        };
        bool set_priority(double priority) {
            if(this->cancel()) {
                std::shared_ptr<Task> taskPtr = std::shared_ptr<Task>(
                    new Task(priority, std::move(this->task->func)));
                    this->task->func = [](){};
                {
                    std::unique_lock<std::mutex>lk(pool.m_);
                    pool.tasks.push(std::move(taskPtr));
                }
                return true;
            }
            return false;
        }

        std::future<ReturnT> future;

    private:
        explicit TaskHandle(std::shared_ptr<Task> task, std::future<ReturnT> future, ThreadPool& pool)
            : task(std::move(task)), future(std::move(future)), pool(pool) {}

        std::shared_ptr<Task> task;

        ThreadPool& pool;

        friend class ThreadPool;
    };

    template<class F, class... Args,
             typename = enable_if_t<
                 !disjunction<std::is_bind_expression<decay_t<Args>>...>::value>>
    auto enqueue(double priority, F&& f, Args&&... args)
        -> TaskHandle<result_of_t<decay_t<F>, decay_t<Args>...>> {
        using ReturnT = result_of_t<decay_t<F>, decay_t<Args>...>;

        std::packaged_task<ReturnT()> task_func(
            std::bind(std::forward<F>(f), std::forward<Args>(args)...)
        );

        auto task_ptr = std::make_shared<std::packaged_task<ReturnT()>>(std::move(task_func));
        std::future<ReturnT> future = task_ptr->get_future();
        auto wrapper = [task_ptr]() {
            (*task_ptr)();
        };

        std::shared_ptr<Task> taskPtr = std::shared_ptr<Task>(new Task(priority, wrapper));

        {
            std::unique_lock<std::mutex> lk(this->m_);
            this->tasks.push(taskPtr);
        }
        this->cv_.notify_one();

        return TaskHandle<ReturnT>{taskPtr, std::move(future), *this};
    };

    void start() {
        for(size_t i = 0; i < numWorkers; ++i) {
            this->workers.emplace_back([this](){ this->worker(); });
        }
    };

    void shutdown() {
        // graceful shutdown
        this->stop_.store(true, std::memory_order_release);
        this->cv_.notify_all();

        for (std::thread &w : this->workers) {
            if (w.joinable()) w.join();
        }
    };

    ~ThreadPool() {
        shutdown();
    }

private:
    void worker() {
        for(;;) {
            // Acquire the task
            std::shared_ptr<Task> candidate;
            {
                std::unique_lock<std::mutex> lk(m_);
                cv_.wait(lk, [&]{ return !tasks.empty() || stop_.load(std::memory_order_acquire); });

                if(this->stop_.load(std::memory_order_acquire)) [[unlikely]] break;

                while (!tasks.empty()) {
                    candidate = tasks.top();
                    tasks.pop();

                    if (candidate->state.load(std::memory_order_acquire) == Task::Cancel) {
                        candidate.reset();
                        continue;
                    }
                    break;  // Got a valid task, exit loop
                }
            }

            if(candidate) {
                candidate->state.store(Task::State::Running, std::memory_order_release);
                candidate->func();  // std::packaged_task will handle exception
            }
        }
    };

    size_t numWorkers;
    std::vector<std::thread> workers;

    struct TaskPtrComp {
        bool operator()(const std::shared_ptr<Task>& a, const std::shared_ptr<Task>& b) const {
            return a->priority < b->priority;
        }
    };
    std::priority_queue<std::shared_ptr<Task>, std::vector<std::shared_ptr<Task>>, TaskPtrComp> tasks;

    std::mutex m_;
    std::condition_variable cv_;
    std::atomic<bool> stop_;
};

}
