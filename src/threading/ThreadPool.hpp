#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace hs {

// Fixed-size pool with a bounded queue. The bound matters: an unbounded queue
// turns a connection flood into unbounded memory growth, whereas a full queue
// lets the acceptor answer 503 immediately.
class ThreadPool {
public:
    using Task = std::function<void()>;

    ThreadPool(std::size_t threadCount, std::size_t maxQueueSize);
    ~ThreadPool();

    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    // Returns false when the queue is full or the pool is shutting down.
    bool submit(Task task);

    // Wakes every worker, lets them drain what is already queued, then joins.
    void shutdown();

    std::size_t queuedTasks() const;
    std::size_t threadCount() const noexcept { return workers_.size(); }
    std::size_t busyWorkers() const noexcept { return busy_.load(std::memory_order_relaxed); }

private:
    void workerLoop();

    mutable std::mutex mutex_;
    std::condition_variable available_;
    std::deque<Task> queue_;
    std::vector<std::thread> workers_;
    std::atomic<std::size_t> busy_{0};
    std::size_t maxQueueSize_;
    bool stopping_ = false;
};

}  // namespace hs
