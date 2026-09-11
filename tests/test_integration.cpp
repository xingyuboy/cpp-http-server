#include <filesystem>
#include <fstream>
#include <random>
#include <thread>

#include "TestFramework.hpp"
#include "config/Config.hpp"
#include "json/Json.hpp"
#include "net/Socket.hpp"
#include "server/Server.hpp"

using namespace hs;

namespace {

struct RawResponse {
    int status = 0;
    std::string headers;
    std::string body;
};

// Minimal client: writes a raw request and reads until Content-Length is
// satisfied (or the peer closes). Deliberately independent of the server's own
// parser so a bug in the parser cannot hide itself here.
RawResponse exchange(std::uint16_t port, const std::string& request, bool expectBody = true) {
    TcpSocket socket = connectTcp("127.0.0.1", port, 3000);
    socket.sendAll(request);

    std::string raw;
    char buffer[4096];
    std::size_t contentLength = std::string::npos;
    std::size_t headerEnd = std::string::npos;

    while (true) {
        if (headerEnd == std::string::npos) {
            headerEnd = raw.find("\r\n\r\n");
            if (headerEnd != std::string::npos) {
                const std::size_t marker = raw.find("Content-Length: ");
                if (marker != std::string::npos && marker < headerEnd) {
                    contentLength = std::stoul(raw.substr(marker + 16));
                } else {
                    contentLength = 0;
                }
            }
        }
        if (headerEnd != std::string::npos) {
            const std::size_t have = raw.size() - (headerEnd + 4);
            if (!expectBody || have >= contentLength) {
                break;
            }
        }
        const IoResult result = socket.receive(buffer, sizeof(buffer));
        if (result.status != IoStatus::Ok) {
            break;
        }
        raw.append(buffer, result.bytes);
    }

    RawResponse response;
    if (raw.size() > 12) {
        response.status = std::stoi(raw.substr(9, 3));
    }
    if (headerEnd != std::string::npos) {
        response.headers = raw.substr(0, headerEnd);
        response.body = raw.substr(headerEnd + 4);
    }
    return response;
}

std::string httpGet(const std::string& path) {
    return "GET " + path + " HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n";
}

class ServerFixture {
public:
    ServerFixture() {
        base_ = std::filesystem::temp_directory_path() /
                ("hs-integration-" + std::to_string(std::random_device{}()));
        const std::filesystem::path root = base_ / "public";
        std::filesystem::create_directories(root);
        std::ofstream(root / "index.html") << "<h1>integration</h1>";
        std::ofstream(base_ / "secret.txt") << "top secret";

        Config config;
        config.port = 0;  // let the OS pick a free port
        config.documentRoot = root.string();
        config.workerThreads = 4;
        config.requestTimeoutMs = 1000;
        config.keepAliveTimeoutMs = 1000;

        server_ = std::make_unique<Server>(std::move(config));
        server_->start();
    }

    ~ServerFixture() {
        server_->stop();
        std::error_code error;
        std::filesystem::remove_all(base_, error);
    }

    std::uint16_t port() const { return server_->port(); }

private:
    std::filesystem::path base_;
    std::unique_ptr<Server> server_;
};

}  // namespace

TEST(IntegrationServesStaticIndex) {
    ServerFixture server;
    const RawResponse response = exchange(server.port(), httpGet("/"));
    CHECK_EQ(response.status, 200);
    CHECK_EQ(response.body, std::string("<h1>integration</h1>"));
    CHECK(response.headers.find("text/html") != std::string::npos);
}

TEST(IntegrationServesJsonApi) {
    ServerFixture server;
    const RawResponse response = exchange(server.port(), httpGet("/api/users"));
    CHECK_EQ(response.status, 200);

    const auto body = json::parse(response.body);
    REQUIRE(body.has_value());
    CHECK_EQ(body->find("users")->asArray().size(), std::size_t(2));
}

TEST(IntegrationCreatesUserOverHttp) {
    ServerFixture server;
    const std::string payload = R"({"name":"Dave","email":"dave@example.com"})";
    const std::string request = "POST /api/users HTTP/1.1\r\nHost: localhost\r\n"
                                "Content-Type: application/json\r\nConnection: close\r\n"
                                "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n" +
                                payload;

    const RawResponse response = exchange(server.port(), request);
    CHECK_EQ(response.status, 201);
    CHECK(response.headers.find("Location: /api/users/3") != std::string::npos);
}

TEST(IntegrationDeleteReturns204WithoutBody) {
    ServerFixture server;
    const RawResponse response = exchange(
        server.port(), "DELETE /api/users/1 HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n",
        false);
    CHECK_EQ(response.status, 204);
    CHECK(response.body.empty());
}

TEST(IntegrationHeadReturnsHeadersWithoutBody) {
    ServerFixture server;
    const RawResponse response = exchange(
        server.port(), "HEAD / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n", false);
    CHECK_EQ(response.status, 200);
    CHECK(response.headers.find("Content-Length: 20") != std::string::npos);
    CHECK(response.body.empty());
}

TEST(IntegrationUnknownPathReturns404) {
    ServerFixture server;
    CHECK_EQ(exchange(server.port(), httpGet("/missing")).status, 404);
}

TEST(IntegrationMethodMismatchReturns405) {
    ServerFixture server;
    const RawResponse response = exchange(
        server.port(),
        "DELETE /api/users HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    CHECK_EQ(response.status, 405);
    CHECK(response.headers.find("Allow:") != std::string::npos);
}

TEST(IntegrationMalformedRequestReturns400) {
    ServerFixture server;
    const RawResponse response = exchange(server.port(), "NOT-HTTP\r\n\r\n");
    CHECK_EQ(response.status, 400);
}

TEST(IntegrationUnknownMethodReturns501) {
    ServerFixture server;
    const RawResponse response = exchange(
        server.port(), "PATCH / HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    CHECK_EQ(response.status, 501);
}

TEST(IntegrationTraversalCannotReadOutsideRoot) {
    ServerFixture server;
    const RawResponse response = exchange(server.port(), httpGet("/../secret.txt"));
    CHECK(response.status == 404 || response.status == 403);
    CHECK(response.body.find("top secret") == std::string::npos);
}

TEST(IntegrationOversizedBodyReturns413) {
    ServerFixture server;
    const std::string request = "POST /api/users HTTP/1.1\r\nHost: localhost\r\n"
                                "Content-Length: 99999999\r\nConnection: close\r\n\r\n";
    CHECK_EQ(exchange(server.port(), request).status, 413);
}

TEST(IntegrationIdleRequestTimesOutWith408) {
    ServerFixture server;
    // Headers are never terminated, so the server must give up on its own.
    TcpSocket socket = connectTcp("127.0.0.1", server.port(), 3000);
    socket.sendAll("GET / HTTP/1.1\r\nHost: localhost\r\n");

    std::string raw;
    char buffer[1024];
    while (true) {
        const IoResult result = socket.receive(buffer, sizeof(buffer));
        if (result.status != IoStatus::Ok) break;
        raw.append(buffer, result.bytes);
        if (raw.find("\r\n\r\n") != std::string::npos) break;
    }
    REQUIRE(raw.size() > 12);
    CHECK_EQ(std::stoi(raw.substr(9, 3)), 408);
}

TEST(IntegrationKeepAliveServesTwoRequestsOnOneSocket) {
    ServerFixture server;
    TcpSocket socket = connectTcp("127.0.0.1", server.port(), 3000);
    socket.sendAll("GET /api/health HTTP/1.1\r\nHost: localhost\r\n\r\n");

    char buffer[4096];
    IoResult result = socket.receive(buffer, sizeof(buffer));
    REQUIRE(result.status == IoStatus::Ok);
    std::string first(buffer, result.bytes);
    CHECK(first.find("200 OK") != std::string::npos);
    CHECK(first.find("keep-alive") != std::string::npos);

    socket.sendAll("GET /api/health HTTP/1.1\r\nHost: localhost\r\nConnection: close\r\n\r\n");
    result = socket.receive(buffer, sizeof(buffer));
    REQUIRE(result.status == IoStatus::Ok);
    CHECK(std::string(buffer, result.bytes).find("200 OK") != std::string::npos);
}

TEST(IntegrationHandlesConcurrentClients) {
    ServerFixture server;
    constexpr int kClients = 16;
    constexpr int kRequestsEach = 10;

    std::atomic<int> successes{0};
    std::vector<std::thread> clients;
    clients.reserve(kClients);
    for (int i = 0; i < kClients; ++i) {
        clients.emplace_back([&] {
            for (int r = 0; r < kRequestsEach; ++r) {
                if (exchange(server.port(), httpGet("/api/users")).status == 200) {
                    successes.fetch_add(1);
                }
            }
        });
    }
    for (auto& client : clients) {
        client.join();
    }
    CHECK_EQ(successes.load(), kClients * kRequestsEach);
}

TEST(IntegrationBenchmarkEndpointReportsCounters) {
    ServerFixture server;
    exchange(server.port(), httpGet("/api/users"));
    const RawResponse response = exchange(server.port(), httpGet("/api/benchmark"));
    CHECK_EQ(response.status, 200);

    const auto body = json::parse(response.body);
    REQUIRE(body.has_value());
    CHECK(body->find("requests_total")->asNumber() >= 1.0);
    CHECK(body->find("worker_threads")->asNumber() == 4.0);
}
