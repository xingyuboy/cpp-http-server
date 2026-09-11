// Small closed-loop load generator: N connections, each sending keep-alive
// requests back to back for a fixed duration. Closed-loop means the reported
// rate is bounded by latency, which is what you want when comparing server
// configurations on one machine.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "net/Socket.hpp"

namespace {

struct Options {
    std::string host = "127.0.0.1";
    std::uint16_t port = 8080;
    std::string path = "/api/benchmark";
    int connections = 8;
    int seconds = 10;
};

struct ThreadResult {
    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t reconnects = 0;
    std::vector<double> latenciesMs;
};

// Reads one response, using Content-Length to know when the body is complete.
bool readResponse(hs::TcpSocket& socket, std::string& buffer) {
    std::size_t headerEnd = buffer.find("\r\n\r\n");
    std::size_t contentLength = 0;
    char chunk[8192];

    while (true) {
        if (headerEnd != std::string::npos) {
            const std::size_t marker = buffer.find("Content-Length: ");
            contentLength = marker == std::string::npos || marker > headerEnd
                                ? 0
                                : std::strtoul(buffer.c_str() + marker + 16, nullptr, 10);
            if (buffer.size() - headerEnd - 4 >= contentLength) {
                buffer.erase(0, headerEnd + 4 + contentLength);
                return true;
            }
        }
        const hs::IoResult result = socket.receive(chunk, sizeof(chunk));
        if (result.status != hs::IoStatus::Ok) {
            return false;
        }
        buffer.append(chunk, result.bytes);
        if (headerEnd == std::string::npos) {
            headerEnd = buffer.find("\r\n\r\n");
        }
    }
}

ThreadResult runConnection(const Options& options, const std::string& request,
                           std::chrono::steady_clock::time_point deadline) {
    ThreadResult result;

    // The server closes a keep-alive connection once it hits its per-connection
    // request cap, so reconnecting is expected behaviour, not an error.
    while (std::chrono::steady_clock::now() < deadline) {
        try {
            hs::TcpSocket socket = hs::connectTcp(options.host, options.port, 5000);
            socket.setNoDelay(true);
            std::string buffer;

            while (std::chrono::steady_clock::now() < deadline) {
                const auto started = std::chrono::steady_clock::now();
                if (socket.sendAll(request).status != hs::IoStatus::Ok ||
                    !readResponse(socket, buffer)) {
                    ++result.reconnects;
                    break;
                }
                const auto elapsed = std::chrono::steady_clock::now() - started;
                result.latenciesMs.push_back(
                    std::chrono::duration<double, std::milli>(elapsed).count());
                ++result.completed;
            }
        } catch (const std::exception& error) {
            ++result.failed;
            if (result.failed > 5) {
                std::cerr << "giving up on this connection: " << error.what() << "\n";
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    return result;
}

double percentile(const std::vector<double>& sorted, double fraction) {
    if (sorted.empty()) {
        return 0.0;
    }
    const auto index = static_cast<std::size_t>(fraction * static_cast<double>(sorted.size() - 1));
    return sorted[index];
}

}  // namespace

int main(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc - 1; ++i) {
        const std::string flag = argv[i];
        if (flag == "--host") options.host = argv[++i];
        else if (flag == "--port") options.port = static_cast<std::uint16_t>(std::stoi(argv[++i]));
        else if (flag == "--path") options.path = argv[++i];
        else if (flag == "--connections") options.connections = std::stoi(argv[++i]);
        else if (flag == "--seconds") options.seconds = std::stoi(argv[++i]);
        else {
            std::cerr << "unknown option: " << flag << "\n";
            return 2;
        }
    }

    const std::string request = "GET " + options.path + " HTTP/1.1\r\nHost: " + options.host +
                                "\r\nConnection: keep-alive\r\n\r\n";

    std::cout << "benchmarking http://" << options.host << ':' << options.port << options.path
              << " with " << options.connections << " connections for " << options.seconds
              << "s\n";

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(options.seconds);
    const auto started = std::chrono::steady_clock::now();

    std::vector<std::thread> threads;
    std::vector<ThreadResult> results(static_cast<std::size_t>(options.connections));
    threads.reserve(results.size());
    for (std::size_t i = 0; i < results.size(); ++i) {
        threads.emplace_back([&, i] { results[i] = runConnection(options, request, deadline); });
    }
    for (auto& thread : threads) {
        thread.join();
    }

    const double elapsedSeconds =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - started).count();

    std::uint64_t completed = 0;
    std::uint64_t failed = 0;
    std::uint64_t reconnects = 0;
    std::vector<double> latencies;
    for (const ThreadResult& result : results) {
        completed += result.completed;
        failed += result.failed;
        reconnects += result.reconnects;
        latencies.insert(latencies.end(), result.latenciesMs.begin(), result.latenciesMs.end());
    }
    std::sort(latencies.begin(), latencies.end());

    double total = 0.0;
    for (const double value : latencies) {
        total += value;
    }

    std::cout << std::fixed << std::setprecision(3)
              << "requests:      " << completed << " (" << failed << " failed, " << reconnects
              << " reconnects)\n"
              << "duration:      " << elapsedSeconds << " s\n"
              << "requests/sec:  " << static_cast<double>(completed) / elapsedSeconds << "\n"
              << "latency avg:   " << (latencies.empty() ? 0.0 : total / static_cast<double>(latencies.size())) << " ms\n"
              << "latency p50:   " << percentile(latencies, 0.50) << " ms\n"
              << "latency p95:   " << percentile(latencies, 0.95) << " ms\n"
              << "latency p99:   " << percentile(latencies, 0.99) << " ms\n"
              << "latency max:   " << (latencies.empty() ? 0.0 : latencies.back()) << " ms\n";
    return 0;
}
