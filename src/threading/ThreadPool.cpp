#include "threading/ThreadPool.hpp"

#include "logging/Logger.hpp"

namespace hs {

ThreadPool::ThreadPool(std::size_t threadCount, std::size_t maxQueueSize)
    : maxQueueSize_(maxQueueSize) {
    workers_.reserve(threadCount);
    for (std::size_t i = 0; i < threadCount; ++i) {
        workers_.emplace_back([this] { workerLoop(); });
    }
}

ThreadPool::~ThreadPool() {
    shutdown();
}

bool ThreadPool::submit(Task task) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_ || queue_.size() >= maxQueueSize_) {
            return false;
        }
        queue_.push_back(std::move(task));
    }
    // Notifying outside the lock avoids the woken thread immediately blocking
    // on a mutex this thread still holds.
    available_.notify_one();
    return true;
}

void ThreadPool::shutdown() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            return;
        }
        stopping_ = true;
    }
    available_.notify_all();
    for (auto& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
    workers_.clear();
}

std::size_t ThreadPool::queuedTasks() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return queue_.size();
}

void ThreadPool::workerLoop() {
    while (true) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            available_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
            if (queue_.empty()) {
                return;  // only reachable once stopping_ is set
            }
            task = std::move(queue_.front());
            queue_.pop_front();
        }

        busy_.fetch_add(1, std::memory_order_relaxed);
        try {
            task();
        } catch (const std::exception& error) {
            // A task must never escape into std::thread's handler: that calls
            // std::terminate and takes the whole server down.
            LOG_ERROR("unhandled exception in worker task: " << error.what());
        } catch (...) {
            LOG_ERROR("unhandled non-standard exception in worker task");
        }
        busy_.fetch_sub(1, std::memory_order_relaxed);
    }
}

}  // namespace hs
