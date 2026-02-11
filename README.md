# ThreadPool

A header-only C++ thread pool with priority scheduling, task cancellation, and dynamic priority adjustment.

## Features

- Priority-based task scheduling (higher priority executes first)
- Task cancellation while queued
- Dynamic priority adjustment via `set_priority()`
- Return value support via `std::future`
- C++11/14/17 compatible
- Header-only, no dependencies

## Usage

```cpp
#include "include/ThreadPool.hpp"

int main() {
    // Create pool with 4 workers (starts immediately)
    thread_pool::ThreadPool pool(4);
    
    // Or create inactive pool and start later
    // thread_pool::ThreadPool pool(4, false);
    // pool.start();

    // Submit task with priority and arguments
    auto handle = pool.enqueue(1.0, [](int x) {
        return x * 2;
    }, 21);

    // Get result
    int result = handle.future.get();  // 42

    // Pool shuts down gracefully on destruction
}
```

## API

### ThreadPool

| Method | Description |
|--------|-------------|
| `ThreadPool(size_t n, bool active = true)` | Create pool with n workers |
| `start()` | Start worker threads (if created inactive) |
| `shutdown()` | Graceful shutdown, waits for running tasks |
| `enqueue(priority, func, args...)` | Submit task, returns `TaskHandle` |

### TaskHandle<T>

| Method | Description |
|--------|-------------|
| `future` | `std::future<T>` for the result |
| `get_state()` | Returns `Queued`, `Running`, `Done`, or `Cancel` |
| `cancel()` | Cancel if still queued, returns success |
| `set_priority(double)` | Change priority if still queued |

## Notes

- Arguments are copied/moved into the task. Use `std::ref()` for references, but ensure the referenced object outlives the task execution.
- `std::bind` expressions are not allowed as arguments (compile-time check).
- Priority is a `double`; higher values execute first.

## Build Example

```bash
g++ -std=c++17 -pthread examples/example.cpp -o example
./example
```

## License

MIT
