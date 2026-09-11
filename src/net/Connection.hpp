#pragma once

#include <atomic>
#include <string>

#include "config/Config.hpp"
#include "net/Socket.hpp"
#include "routing/Router.hpp"
#include "server/Metrics.hpp"

namespace hs {

struct ConnectionContext {
    const Config& config;
    const Router& router;
    Metrics& metrics;
    // Watched while waiting for bytes so a worker parked on an idle keep-alive
    // connection does not hold up shutdown for the full timeout.
    const std::atomic<bool>& running;
};

// Runs the whole lifetime of one client connection on a worker thread:
// read -> parse -> route -> write, repeated while keep-alive holds.
void serveConnection(TcpSocket socket, const std::string& peerAddress,
                     const ConnectionContext& context);

}  // namespace hs
