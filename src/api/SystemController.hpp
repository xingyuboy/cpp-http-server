#pragma once

#include "routing/Router.hpp"
#include "server/Metrics.hpp"
#include "threading/ThreadPool.hpp"

namespace hs {

// /api/health and /api/benchmark. The benchmark endpoint deliberately does
// almost no work so that load tests measure the server, not the handler.
void registerSystemRoutes(Router& router, const Metrics& metrics, const ThreadPool& pool);

}  // namespace hs
