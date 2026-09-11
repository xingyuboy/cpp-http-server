#include "config/Config.hpp"

#include <algorithm>
#include <charconv>
#include <fstream>
#include <sstream>
#include <thread>

namespace hs {
namespace {

std::string_view trim(std::string_view value) {
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\r'; };
    while (!value.empty() && isSpace(value.front())) value.remove_prefix(1);
    while (!value.empty() && isSpace(value.back())) value.remove_suffix(1);
    return value;
}

[[noreturn]] void reject(std::size_t line, const std::string& message) {
    throw ConfigError("config line " + std::to_string(line) + ": " + message);
}

long long parseNumber(std::string_view text, std::size_t line, const std::string& key) {
    long long value = 0;
    const auto* last = text.data() + text.size();
    const auto result = std::from_chars(text.data(), last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        reject(line, key + " expects an integer, got '" + std::string(text) + "'");
    }
    return value;
}

}  // namespace

Config Config::parse(std::string_view text) {
    Config config;
    std::istringstream input((std::string(text)));
    std::string rawLine;
    std::size_t lineNumber = 0;

    while (std::getline(input, rawLine)) {
        ++lineNumber;
        std::string_view line = trim(rawLine);
        // Everything after '#' is a comment, including at the end of a line.
        // No setting needs a '#' in its value, so this needs no escaping rule.
        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) {
            line = trim(line.substr(0, comment));
        }
        if (line.empty()) {
            continue;
        }

        const std::size_t equals = line.find('=');
        if (equals == std::string_view::npos) {
            reject(lineNumber, "expected key = value");
        }
        const std::string key(trim(line.substr(0, equals)));
        const std::string_view value = trim(line.substr(equals + 1));
        if (key.empty() || value.empty()) {
            reject(lineNumber, "empty key or value");
        }

        if (key == "bind_address") {
            config.bindAddress = std::string(value);
        } else if (key == "port") {
            const long long port = parseNumber(value, lineNumber, key);
            if (port < 1 || port > 65535) {
                reject(lineNumber, "port must be between 1 and 65535");
            }
            config.port = static_cast<std::uint16_t>(port);
        } else if (key == "document_root") {
            config.documentRoot = std::string(value);
        } else if (key == "worker_threads") {
            const long long threads = parseNumber(value, lineNumber, key);
            if (threads < 0 || threads > 512) {
                reject(lineNumber, "worker_threads must be between 0 and 512");
            }
            config.workerThreads = static_cast<std::size_t>(threads);
        } else if (key == "request_timeout") {
            config.requestTimeoutMs = static_cast<int>(parseNumber(value, lineNumber, key));
        } else if (key == "keep_alive_timeout") {
            config.keepAliveTimeoutMs = static_cast<int>(parseNumber(value, lineNumber, key));
        } else if (key == "max_requests_per_connection") {
            config.maxRequestsPerConnection = static_cast<int>(parseNumber(value, lineNumber, key));
        } else if (key == "max_header_size") {
            config.maxHeaderBytes = static_cast<std::size_t>(parseNumber(value, lineNumber, key));
        } else if (key == "max_body_size") {
            config.maxBodyBytes = static_cast<std::size_t>(parseNumber(value, lineNumber, key));
        } else if (key == "max_queued_connections") {
            config.maxQueuedConnections =
                static_cast<std::size_t>(parseNumber(value, lineNumber, key));
        } else if (key == "listen_backlog") {
            config.listenBacklog = static_cast<int>(parseNumber(value, lineNumber, key));
        } else if (key == "log_level") {
            if (!logLevelFromString(value, config.logLevel)) {
                reject(lineNumber, "unknown log_level '" + std::string(value) + "'");
            }
        } else if (key == "log_file") {
            config.logFile = std::string(value);
        } else {
            reject(lineNumber, "unknown setting '" + key + "'");
        }
    }

    if (config.requestTimeoutMs <= 0) {
        throw ConfigError("request_timeout must be positive");
    }
    if (config.maxQueuedConnections == 0) {
        throw ConfigError("max_queued_connections must be positive");
    }
    return config;
}

Config Config::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) {
        throw ConfigError("cannot open config file: " + path);
    }
    std::ostringstream contents;
    contents << file.rdbuf();
    return parse(contents.str());
}

std::size_t Config::resolvedWorkerThreads() const {
    if (workerThreads > 0) {
        return workerThreads;
    }
    const unsigned hardware = std::thread::hardware_concurrency();
    return hardware == 0 ? 4 : std::max(2u, hardware);
}

}  // namespace hs
