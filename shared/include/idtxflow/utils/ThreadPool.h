#pragma once

/**
 * @file ThreadPool.h
 * @brief A small fixed-size thread pool for offloading blocking work.
 *
 * Network transports run blocking calls (HTTP requests) off the caller's thread.
 * A bounded pool caps how many run at once, giving predictable back-pressure and
 * a fixed thread count instead of spawning an unbounded thread per request.
 *
 * Standard library only; no engine, transport, or USD dependency.
 */

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace idtxflow
{
namespace utils
{
    class ThreadPool
    {
    public:
        /// Start `worker_count` worker threads (at least one).
        explicit ThreadPool(std::size_t worker_count = 2)
        {
            if (worker_count == 0)
            {
                worker_count = 1;
            }
            workers_.reserve(worker_count);
            for (std::size_t i = 0; i < worker_count; ++i)
            {
                workers_.emplace_back([this] { worker_loop(); });
            }
        }

        /// Stop accepting work, let queued tasks drain, and join all workers.
        ~ThreadPool()
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
            }
            cv_.notify_all();
            for (std::thread& t : workers_)
            {
                if (t.joinable())
                {
                    t.join();
                }
            }
        }

        ThreadPool(const ThreadPool&) = delete;
        ThreadPool& operator=(const ThreadPool&) = delete;

        std::size_t worker_count() const { return workers_.size(); }

        /// Queue a task to run on a worker thread. Ignored after shutdown begins.
        void submit(std::function<void()> task)
        {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                if (stopping_)
                {
                    return;
                }
                tasks_.push(std::move(task));
            }
            cv_.notify_one();
        }

    private:
        void worker_loop()
        {
            for (;;)
            {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mutex_);
                    cv_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });

                    // Drain remaining work before exiting so submitted tasks aren't dropped.
                    if (tasks_.empty())
                    {
                        return;
                    }
                    task = std::move(tasks_.front());
                    tasks_.pop();
                }
                task();
            }
        }

        std::mutex                        mutex_;
        std::condition_variable           cv_;
        std::queue<std::function<void()>> tasks_;
        std::vector<std::thread>          workers_;
        bool                              stopping_ = false;
    };

} // namespace utils
} // namespace idtxflow
