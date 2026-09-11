#pragma once

#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <string_view>

namespace hs {

enum class LogLevel { Debug, Info, Warning, Error };

std::string_view logLevelName(LogLevel level);
bool logLevelFromString(std::string_view text, LogLevel& out);

// A process-wide singleton is the one piece of global state in the project.
// Threading a logger reference through every class would add noise without
// buying anything, since there is exactly one log destination.
class Logger {
public:
    static Logger& instance();

    void setLevel(LogLevel level);
    LogLevel level() const;
    bool enabled(LogLevel level) const;

    // Opening a file is optional; console output stays on either way.
    void setLogFile(const std::string& path);

    void write(LogLevel level, std::string_view message);

private:
    Logger() = default;

    mutable std::mutex mutex_;
    LogLevel level_ = LogLevel::Info;
    std::ofstream file_;
};

}  // namespace hs

// The stream form avoids depending on <format>, which older toolchains still
// ship incomplete, and the level check happens before any formatting work.
#define HS_LOG(levelValue, expression)                          \
    do {                                                        \
        if (::hs::Logger::instance().enabled(levelValue)) {      \
            std::ostringstream hsLogStream;                      \
            hsLogStream << expression;                           \
            ::hs::Logger::instance().write(levelValue, hsLogStream.str()); \
        }                                                       \
    } while (false)

#define LOG_DEBUG(expression) HS_LOG(::hs::LogLevel::Debug, expression)
#define LOG_INFO(expression) HS_LOG(::hs::LogLevel::Info, expression)
#define LOG_WARN(expression) HS_LOG(::hs::LogLevel::Warning, expression)
#define LOG_ERROR(expression) HS_LOG(::hs::LogLevel::Error, expression)
