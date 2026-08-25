/**
 * @file ThreadPoolTests.cpp
 * @brief Verifies the fixed-size thread pool runs submitted work and never runs
 *        more tasks at once than it has workers.
 */

#include "test_framework.h"

#include <idtxflow/utils/ThreadPool.h>

#include <atomic>
#include <chrono>
#include <thread>

TEST(ThreadPool, RunsSubmittedTasks)
{
    idtxflow::utils::ThreadPool pool(2);

    std::atomic<int> counter{0};
    const int total = 100;
    for (int i = 0; i < total; ++i)
    {
        pool.submit([&counter] { counter.fetch_add(1, std::memory_order_relaxed); });
    }

    // Wait until every task has run (or a generous timeout elapses).
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (counter.load(std::memory_order_relaxed) < total &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CHECK_EQ(counter.load(std::memory_order_relaxed), total);
}

TEST(ThreadPool, BoundedConcurrency)
{
    const std::size_t workers = 3;
    idtxflow::utils::ThreadPool pool(workers);
    CHECK_EQ(pool.worker_count(), workers);

    std::atomic<int> active{0};
    std::atomic<int> peak{0};
    std::atomic<int> done{0};

    const int total = 30;
    for (int i = 0; i < total; ++i)
    {
        pool.submit([&active, &peak, &done] {
            const int now = active.fetch_add(1, std::memory_order_acq_rel) + 1;
            int prev = peak.load(std::memory_order_relaxed);
            while (now > prev && !peak.compare_exchange_weak(prev, now))
            {
                // retry until peak reflects the observed maximum
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            active.fetch_sub(1, std::memory_order_acq_rel);
            done.fetch_add(1, std::memory_order_acq_rel);
        });
    }

    // Wait until every task has finished, so `peak` reflects the whole run.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (done.load(std::memory_order_acquire) < total &&
           std::chrono::steady_clock::now() < deadline)
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }

    CHECK_EQ(done.load(std::memory_order_acquire), total);
    // Concurrency must never exceed the configured worker count.
    CHECK(peak.load(std::memory_order_relaxed) <= static_cast<int>(workers));
    CHECK(peak.load(std::memory_order_relaxed) >= 1);
}
