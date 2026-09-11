#include <atomic>
#include <chrono>
#include <thread>

#include "TestFramework.hpp"
#include "threading/ThreadPool.hpp"

using namespace hs;

TEST(RunsEverySubmittedTask) {
    ThreadPool pool(4, 1024);
    std::atomic<int> counter{0};
    for (int i = 0; i < 500; ++i) {
        CHECK(pool.submit([&counter] { counter.fetch_add(1); }));
    }
    pool.shutdown();  // drains the queue before joining
    CHECK_EQ(counter.load(), 500);
}

TEST(RejectsSubmissionWhenQueueIsFull) {
    ThreadPool pool(1, 2);
    std::atomic<bool> release{false};
    pool.submit([&release] {
        while (!release.load()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    // The worker is parked on the first task, so the queue fills at its bound.
    int accepted = 0;
    for (int i = 0; i < 10; ++i) {
        if (pool.submit([] {})) {
            ++accepted;
        }
    }
    CHECK(accepted <= 2);
    release.store(true);
}

TEST(SurvivesThrowingTask) {
    ThreadPool pool(2, 16);
    std::atomic<int> completed{0};
    pool.submit([] { throw std::runtime_error("boom"); });
    pool.submit([&completed] { completed.fetch_add(1); });
    pool.shutdown();
    CHECK_EQ(completed.load(), 1);
}

TEST(ShutdownIsIdempotent) {
    ThreadPool pool(2, 16);
    pool.shutdown();
    pool.shutdown();
    CHECK(!pool.submit([] {}));
}

TEST(ReportsThreadCount) {
    ThreadPool pool(3, 16);
    CHECK_EQ(pool.threadCount(), std::size_t(3));
}
