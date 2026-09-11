#include <atomic>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "config/Config.hpp"
#include "logging/Logger.hpp"
#include "net/Socket.hpp"
#include "server/Server.hpp"

namespace {

// A signal handler may only touch a lock-free atomic, so it does nothing but
// flip this flag; main() polls it and performs the real shutdown.
std::atomic<bool> g_shutdownRequested{false};

extern "C" void handleSignal(int) {
    g_shutdownRequested.store(true, std::memory_order_relaxed);
}

void printUsage(const char* program) {
    std::cout << "Usage: " << program << " [options]\n"
              << "  --config <path>   configuration file (default: server.conf if present)\n"
              << "  --port <number>   override the configured port\n"
              << "  --root <path>     override the document root\n"
              << "  --log-level <l>   debug | info | warning | error\n"
              << "  --help            show this message\n";
}

}  // namespace

int main(int argc, char** argv) {
    std::string configPath = "server.conf";
    std::string portOverride;
    std::string rootOverride;
    std::string logLevelOverride;

    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        const auto next = [&](std::string& target) {
            if (i + 1 >= argc) {
                throw std::runtime_error(argument + " requires a value");
            }
            target = argv[++i];
        };
        try {
            if (argument == "--help" || argument == "-h") {
                printUsage(argv[0]);
                return 0;
            } else if (argument == "--config") {
                next(configPath);
            } else if (argument == "--port") {
                next(portOverride);
            } else if (argument == "--root") {
                next(rootOverride);
            } else if (argument == "--log-level") {
                next(logLevelOverride);
            } else {
                std::cerr << "unknown argument: " << argument << "\n";
                printUsage(argv[0]);
                return 2;
            }
        } catch (const std::exception& error) {
            std::cerr << error.what() << "\n";
            return 2;
        }
    }

    hs::Config config;
    try {
        std::ifstream probe(configPath);
        if (probe) {
            probe.close();
            config = hs::Config::loadFromFile(configPath);
        } else {
            std::cout << "no config file at " << configPath << ", using defaults\n";
        }

        if (!portOverride.empty()) {
            config.port = static_cast<std::uint16_t>(std::stoi(portOverride));
        }
        if (!rootOverride.empty()) {
            config.documentRoot = rootOverride;
        }
        if (!logLevelOverride.empty() &&
            !hs::logLevelFromString(logLevelOverride, config.logLevel)) {
            std::cerr << "unknown log level: " << logLevelOverride << "\n";
            return 2;
        }
    } catch (const std::exception& error) {
        std::cerr << "configuration error: " << error.what() << "\n";
        return 1;
    }

    hs::Logger::instance().setLevel(config.logLevel);
    if (!config.logFile.empty()) {
        hs::Logger::instance().setLogFile(config.logFile);
    }

    std::signal(SIGINT, handleSignal);
    std::signal(SIGTERM, handleSignal);

    try {
        hs::Server server(std::move(config));
        server.start();

        while (!g_shutdownRequested.load(std::memory_order_relaxed) && server.running()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "\n";  // keeps the log readable after a ^C echo
        server.stop();
    } catch (const std::exception& error) {
        LOG_ERROR("fatal: " << error.what());
        return 1;
    }

    return 0;
}
