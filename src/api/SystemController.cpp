#include "api/SystemController.hpp"

#include "json/Json.hpp"

namespace hs {

void registerSystemRoutes(Router& router, const Metrics& metrics, const ThreadPool& pool) {
    router.get("/api/health", [](const HttpRequest&) {
        return HttpResponse::json(200, json::serialize(json::Object{{"status", "ok"}}));
    });

    router.get("/api/benchmark", [&metrics, &pool](const HttpRequest&) {
        using std::memory_order_relaxed;
        json::Object payload{
            {"uptime_seconds", metrics.uptimeSeconds()},
            {"requests_total",
             static_cast<double>(metrics.requestsTotal.load(memory_order_relaxed))},
            {"connections_accepted",
             static_cast<double>(metrics.connectionsAccepted.load(memory_order_relaxed))},
            {"connections_rejected",
             static_cast<double>(metrics.connectionsRejected.load(memory_order_relaxed))},
            {"active_connections",
             static_cast<double>(metrics.activeConnections.load(memory_order_relaxed))},
            {"bytes_sent", static_cast<double>(metrics.bytesSent.load(memory_order_relaxed))},
            {"worker_threads", static_cast<double>(pool.threadCount())},
            {"queued_tasks", static_cast<double>(pool.queuedTasks())},
        };

        json::Object statuses;
        static const char* kNames[] = {"1xx", "1xx", "2xx", "3xx", "4xx", "5xx"};
        for (std::size_t i = 2; i < 6; ++i) {
            statuses.emplace_back(
                kNames[i],
                static_cast<double>(metrics.responsesByClass[i].load(memory_order_relaxed)));
        }
        payload.emplace_back("responses", std::move(statuses));

        return HttpResponse::json(200, json::serialize(json::Value(std::move(payload))));
    });
}

}  // namespace hs
