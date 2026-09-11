#include "logging/Logger.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>

namespace hs {
namespace {

std::string timestamp() {
    using clock = std::chrono::system_clock;
    const auto now = clock::now();
    const auto time = clock::to_time_t(now);
    const auto millis =
        std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;

    std::tm parts{};
#ifdef _WIN32
    localtime_s(&parts, &time);
#else
    localtime_r(&time, &parts);
#endif

    std::ostringstream out;
    out << std::put_time(&parts, "%Y-%m-%d %H:%M:%S") << '.' << std::setfill('0') << std::setw(3)
        << millis.count();
    return out.str();
}

}  // namespace

std::string_view logLevelName(LogLevel level) {
    switch (level) {
        case LogLevel::Debug: return "DEBUG";
        case LogLevel::Info: return "INFO";
        case LogLevel::Warning: return "WARNING";
        case LogLevel::Error: return "ERROR";
    }
    return "INFO";
}

bool logLevelFromString(std::string_view text, LogLevel& out) {
    if (text == "debug" || text == "DEBUG") { out = LogLevel::Debug; return true; }
    if (text == "info" || text == "INFO") { out = LogLevel::Info; return true; }
    if (text == "warning" || text == "WARNING") { out = LogLevel::Warning; return true; }
    if (text == "error" || text == "ERROR") { out = LogLevel::Error; return true; }
    return false;
}

Logger& Logger::instance() {
    static Logger logger;
    return logger;
}

void Logger::setLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(mutex_);
    level_ = level;
}

LogLevel Logger::level() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return level_;
}

bool Logger::enabled(LogLevel level) const {
    std::lock_guard<std::mutex> lock(mutex_);
    return static_cast<int>(level) >= static_cast<int>(level_);
}

void Logger::setLogFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(mutex_);
    file_.open(path, std::ios::app);
    if (!file_) {
        std::cerr << "[WARNING] cannot open log file " << path << '\n';
    }
}

void Logger::write(LogLevel level, std::string_view message) {
    std::ostringstream line;
    line << timestamp() << " [" << logLevelName(level) << "] " << message << '\n';
    const std::string text = line.str();

    // One lock for both sinks keeps lines from interleaving across worker threads.
    std::lock_guard<std::mutex> lock(mutex_);
    std::ostream& console = level == LogLevel::Error ? std::cerr : std::cout;
    console << text;
    console.flush();
    if (file_.is_open()) {
        file_ << text;
        file_.flush();
    }
}

}  // namespace hs
