#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>

#include "api/UserController.hpp"
#include "api/UserStore.hpp"
#include "config/Config.hpp"
#include "http/StaticFiles.hpp"
#include "net/Socket.hpp"
#include "routing/Router.hpp"
#include "server/Metrics.hpp"
#include "threading/ThreadPool.hpp"

namespace hs {

class Server {
public:
    explicit Server(Config config);
    ~Server();

    Server(const Server&) = delete;
    Server& operator=(const Server&) = delete;

    // Binds and starts the accept loop on its own thread, so callers (main and
    // the integration tests alike) stay in control.
    void start();
    void stop();
    void wait();

    bool running() const noexcept { return running_.load(std::memory_order_relaxed); }
    std::uint16_t port() const noexcept { return boundPort_; }
    Router& router() noexcept { return router_; }
    const Metrics& metrics() const noexcept { return metrics_; }

private:
    void acceptLoop();
    void rejectConnection(TcpSocket socket);

    Config config_;
    Metrics metrics_;
    UserStore users_;
    // The controller must outlive the router: its route handlers are lambdas
    // that capture it, so declaring it here (not as a constructor local) is
    // what keeps those captures valid.
    UserController userController_{users_};
    Router router_;
    std::unique_ptr<StaticFileHandler> staticFiles_;
    std::unique_ptr<ThreadPool> pool_;
    TcpListener listener_;
    std::thread acceptThread_;
    std::atomic<bool> running_{false};
    std::uint16_t boundPort_ = 0;
};

}  // namespace hs
