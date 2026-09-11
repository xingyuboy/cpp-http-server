#include "net/Connection.hpp"

#include <algorithm>
#include <chrono>
#include <iomanip>
#include <sstream>

#include "http/HttpParser.hpp"
#include "logging/Logger.hpp"

namespace hs {
namespace {

constexpr std::size_t kReadBufferSize = 16 * 1024;

// Reads are sliced so the shutdown flag gets checked regularly instead of only
// when the full request or keep-alive timeout expires.
constexpr int kShutdownPollMs = 250;

std::string formatMillis(std::chrono::steady_clock::duration elapsed) {
    const double millis = std::chrono::duration<double, std::milli>(elapsed).count();
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << millis;
    return out.str();
}

bool writeResponse(TcpSocket& socket, const HttpResponse& response, bool includeBody,
                   Metrics& metrics) {
    const std::string bytes = response.serialize(includeBody);
    const IoResult result = socket.sendAll(bytes);
    metrics.bytesSent.fetch_add(result.bytes, std::memory_order_relaxed);
    metrics.recordStatus(response.status());
    return result.status == IoStatus::Ok;
}

}  // namespace

void serveConnection(TcpSocket socket, const std::string& peerAddress,
                     const ConnectionContext& context) {
    const Config& config = context.config;
    Metrics& metrics = context.metrics;

    metrics.activeConnections.fetch_add(1, std::memory_order_relaxed);
    struct ActiveGuard {
        Metrics& metrics;
        ~ActiveGuard() { metrics.activeConnections.fetch_sub(1, std::memory_order_relaxed); }
    } activeGuard{metrics};

    socket.setNoDelay(true);
    socket.setSendTimeout(config.requestTimeoutMs);

    ParserLimits limits;
    limits.maxHeaderBytes = config.maxHeaderBytes;
    limits.maxBodyBytes = config.maxBodyBytes;

    std::string pending;  // bytes already read that belong to the next request
    int served = 0;

    while (served < config.maxRequestsPerConnection) {
        RequestParser parser(limits);
        auto state = ParseState::NeedMore;

        if (!pending.empty()) {
            std::string carried;
            carried.swap(pending);
            state = parser.consume(carried);
        }

        // An idle keep-alive connection may wait longer than a half-sent
        // request, so the budget depends on whether bytes have arrived yet.
        const bool firstRequest = served == 0;
        const int idleBudgetMs =
            firstRequest ? config.requestTimeoutMs : config.keepAliveTimeoutMs;
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(idleBudgetMs);
        socket.setReceiveTimeout(std::min(kShutdownPollMs, idleBudgetMs));

        std::string buffer(kReadBufferSize, '\0');
        bool clientGone = false;
        bool timedOut = false;
        bool shuttingDown = false;
        bool receivedAnything = state != ParseState::NeedMore;

        while (state == ParseState::NeedMore) {
            const IoResult result = socket.receive(buffer.data(), buffer.size());
            if (result.status == IoStatus::Ok) {
                receivedAnything = true;
                // The clock restarts on progress: the limit is on a stalled
                // request, not on a large one arriving steadily.
                deadline = std::chrono::steady_clock::now() +
                           std::chrono::milliseconds(config.requestTimeoutMs);
                state = parser.consume(std::string_view(buffer.data(), result.bytes));
                continue;
            }
            if (result.status == IoStatus::Timeout) {
                if (!context.running.load(std::memory_order_relaxed)) {
                    shuttingDown = true;
                    break;
                }
                if (std::chrono::steady_clock::now() >= deadline) {
                    timedOut = true;
                    break;
                }
                continue;  // slice expired, but the request still has budget
            }
            clientGone = true;
            break;
        }

        if (shuttingDown) {
            LOG_DEBUG("dropping idle connection from " << peerAddress << " during shutdown");
            return;
        }

        if (clientGone || (timedOut && !receivedAnything)) {
            // A quiet close or an idle keep-alive timeout is normal traffic,
            // not an error worth logging at INFO.
            LOG_DEBUG("connection from " << peerAddress << " closed after " << served
                                         << " request(s)");
            return;
        }

        if (timedOut) {
            LOG_WARN("request timeout from " << peerAddress);
            writeResponse(socket, HttpResponse::error(408, "Request took too long to arrive."),
                          true, metrics);
            socket.shutdownWrite();
            return;
        }

        if (state == ParseState::Failed) {
            LOG_WARN("bad request from " << peerAddress << ": " << parser.errorReason());
            HttpResponse response = HttpResponse::error(parser.errorStatus(), parser.errorReason());
            response.setHeader("Connection", "close");
            writeResponse(socket, response, true, metrics);
            socket.shutdownWrite();
            return;
        }

        HttpRequest& request = parser.request();
        const auto started = std::chrono::steady_clock::now();

        HttpResponse response;
        try {
            response = context.router.dispatch(request);
        } catch (const std::exception& error) {
            // Handlers are application code; a bug there should cost one
            // request, not the process.
            LOG_ERROR("handler threw while serving " << request.target << ": " << error.what());
            response = HttpResponse::error(500, "The request handler failed.");
        }

        const auto elapsed = std::chrono::steady_clock::now() - started;
        metrics.requestsTotal.fetch_add(1, std::memory_order_relaxed);

        ++served;
        const bool lastRequest = served >= config.maxRequestsPerConnection;
        const bool keepAlive = request.keepAlive() && !lastRequest;
        response.setHeader("Connection", keepAlive ? "keep-alive" : "close");
        response.setHeader("Server", "cpp-http-server");

        const bool includeBody = request.method != Method::Head;
        const bool written = writeResponse(socket, response, includeBody, metrics);

        LOG_INFO(request.methodText << ' ' << request.target << ' ' << response.status() << ' '
                                    << formatMillis(elapsed) << "ms " << peerAddress);

        if (!written) {
            LOG_DEBUG("client " << peerAddress << " disconnected before the response was sent");
            return;
        }
        if (!keepAlive) {
            socket.shutdownWrite();
            return;
        }

        pending.assign(parser.leftover());
    }
}

}  // namespace hs
