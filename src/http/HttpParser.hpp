#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "http/HttpRequest.hpp"

namespace hs {

struct ParserLimits {
    std::size_t maxHeaderBytes = 8 * 1024;
    std::size_t maxBodyBytes = 1024 * 1024;
    std::size_t maxTargetBytes = 2048;
};

enum class ParseState { NeedMore, Complete, Failed };

// Incremental parser: the connection loop feeds whatever recv() returned and
// the parser decides when a full message is available. Keeping it free of any
// socket knowledge is what makes the parser unit-testable.
class RequestParser {
public:
    explicit RequestParser(ParserLimits limits = {}) : limits_(limits) {}

    ParseState consume(std::string_view chunk);

    ParseState state() const noexcept { return state_; }
    HttpRequest& request() noexcept { return request_; }
    const HttpRequest& request() const noexcept { return request_; }

    int errorStatus() const noexcept { return errorStatus_; }
    const std::string& errorReason() const noexcept { return errorReason_; }

    // Bytes received after the current message; a pipelined request lives here.
    std::string_view leftover() const;

private:
    ParseState fail(int status, std::string reason);
    bool parseRequestLine(std::string_view line);
    bool parseHeaderLines(std::string_view block);
    bool applyHeaders();

    ParserLimits limits_;
    ParseState state_ = ParseState::NeedMore;
    HttpRequest request_;
    std::string buffer_;
    std::size_t searchOffset_ = 0;
    std::size_t bodyOffset_ = 0;
    std::size_t contentLength_ = 0;
    std::size_t messageEnd_ = 0;
    bool headersComplete_ = false;
    int errorStatus_ = 400;
    std::string errorReason_;
};

}  // namespace hs
