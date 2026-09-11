#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

namespace hs {

// Counters are incremented from every worker thread, so they are atomic.
// Relaxed ordering is enough: nothing else depends on the ordering of these
// increments, and the values are only read for reporting.
struct Metrics {
    std::atomic<std::uint64_t> connectionsAccepted{0};
    std::atomic<std::uint64_t> connectionsRejected{0};
    std::atomic<std::uint64_t> requestsTotal{0};
    std::atomic<std::uint64_t> responsesByClass[6]{};  // indexed by status / 100
    std::atomic<std::uint64_t> bytesSent{0};
    std::atomic<std::int64_t> activeConnections{0};

    const std::chrono::steady_clock::time_point startedAt = std::chrono::steady_clock::now();

    void recordStatus(int status) {
        const std::size_t bucket = static_cast<std::size_t>(status) / 100;
        if (bucket < 6) {
            responsesByClass[bucket].fetch_add(1, std::memory_order_relaxed);
        }
    }

    double uptimeSeconds() const {
        const auto elapsed = std::chrono::steady_clock::now() - startedAt;
        return std::chrono::duration<double>(elapsed).count();
    }
};

}  // namespace hs
