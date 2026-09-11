#pragma once

#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <string_view>

#include "logging/Logger.hpp"

namespace hs {

class ConfigError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct Config {
    std::string bindAddress = "127.0.0.1";
    std::uint16_t port = 8080;
    std::string documentRoot = "./public";
    std::size_t workerThreads = 0;  // 0 means "use hardware_concurrency"
    int requestTimeoutMs = 5000;
    int keepAliveTimeoutMs = 15000;
    int maxRequestsPerConnection = 100;
    std::size_t maxHeaderBytes = 8 * 1024;
    std::size_t maxBodyBytes = 1024 * 1024;
    std::size_t maxQueuedConnections = 256;
    int listenBacklog = 128;
    LogLevel logLevel = LogLevel::Info;
    std::string logFile;

    static Config parse(std::string_view text);
    static Config loadFromFile(const std::string& path);

    std::size_t resolvedWorkerThreads() const;
};

}  // namespace hs
