#include "server/Server.hpp"

#include <utility>

#include "api/SystemController.hpp"
#include "api/UserController.hpp"
#include "logging/Logger.hpp"
#include "net/Connection.hpp"

namespace hs {
namespace {

constexpr int kAcceptPollMs = 200;

}  // namespace

Server::Server(Config config) : config_(std::move(config)) {
    staticFiles_ = std::make_unique<StaticFileHandler>(config_.documentRoot);
    pool_ = std::make_unique<ThreadPool>(config_.resolvedWorkerThreads(),
                                         config_.maxQueuedConnections);

    userController_.registerRoutes(router_);
    registerSystemRoutes(router_, metrics_, *pool_);

    // Anything the API does not claim falls through to the document root.
    router_.setFallback([this](const HttpRequest& request) {
        return staticFiles_->handle(request);
    });
}

Server::~Server() {
    stop();
}

void Server::start() {
    if (running_.load(std::memory_order_relaxed)) {
        return;
    }
    listener_.bindAndListen(config_.bindAddress, config_.port, config_.listenBacklog);
    boundPort_ = listener_.localPort();
    running_.store(true, std::memory_order_relaxed);

    LOG_INFO("server listening on " << config_.bindAddress << ':' << boundPort_ << " with "
                                    << pool_->threadCount() << " worker threads");
    LOG_INFO("document root: " << staticFiles_->root().string());

    acceptThread_ = std::thread([this] { acceptLoop(); });
}

void Server::stop() {
    if (!running_.exchange(false, std::memory_order_relaxed)) {
        return;
    }
    LOG_INFO("shutting down");

    // Order matters: stop accepting first, then let in-flight requests finish,
    // and only then drop the listening socket.
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
    pool_->shutdown();
    listener_.close();

    LOG_INFO("served " << metrics_.requestsTotal.load(std::memory_order_relaxed)
                       << " request(s) across "
                       << metrics_.connectionsAccepted.load(std::memory_order_relaxed)
                       << " connection(s)");
}

void Server::wait() {
    if (acceptThread_.joinable()) {
        acceptThread_.join();
    }
}

void Server::acceptLoop() {
    while (running_.load(std::memory_order_relaxed)) {
        std::string peer;
        auto client = listener_.acceptWithTimeout(kAcceptPollMs, peer);
        if (!client) {
            continue;  // poll timeout, or an accept that failed harmlessly
        }

        metrics_.connectionsAccepted.fetch_add(1, std::memory_order_relaxed);

        // std::function requires a copyable callable, and TcpSocket is
        // deliberately move-only, so ownership is shared into the task.
        auto socket = std::make_shared<TcpSocket>(std::move(*client));
        // The context is rebuilt inside the task rather than captured from
        // this frame: the accept thread is joined before the pool drains, so a
        // reference to a local here would dangle for in-flight requests.
        const bool queued = pool_->submit([this, socket, peer] {
            const ConnectionContext context{config_, router_, metrics_, running_};
            serveConnection(std::move(*socket), peer, context);
        });

        if (!queued) {
            metrics_.connectionsRejected.fetch_add(1, std::memory_order_relaxed);
            LOG_WARN("connection queue full, rejecting " << peer);
            rejectConnection(std::move(*socket));
        }
    }
}

void Server::rejectConnection(TcpSocket socket) {
    HttpResponse response = HttpResponse::error(503, "Server is at capacity, try again shortly.");
    response.setHeader("Connection", "close");
    response.setHeader("Retry-After", "1");
    socket.sendAll(response.serialize());
    socket.shutdownWrite();
    metrics_.recordStatus(503);
}

}  // namespace hs
