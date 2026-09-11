#include "http/HttpParser.hpp"

#include <algorithm>
#include <charconv>

#include "http/Url.hpp"

namespace hs {
namespace {

std::string_view trim(std::string_view value) {
    const auto notSpace = [](char c) { return c != ' ' && c != '\t'; };
    const auto begin = std::find_if(value.begin(), value.end(), notSpace);
    const auto end = std::find_if(value.rbegin(), value.rend(), notSpace).base();
    return begin < end ? std::string_view(&*begin, static_cast<std::size_t>(end - begin))
                       : std::string_view{};
}

bool isTokenChar(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (u <= 32 || u >= 127) return false;
    switch (c) {
        case '(': case ')': case '<': case '>': case '@': case ',': case ';': case ':':
        case '\\': case '"': case '/': case '[': case ']': case '?': case '=': case '{':
        case '}':
            return false;
        default:
            return true;
    }
}

}  // namespace

ParseState RequestParser::fail(int status, std::string reason) {
    state_ = ParseState::Failed;
    errorStatus_ = status;
    errorReason_ = std::move(reason);
    return state_;
}

ParseState RequestParser::consume(std::string_view chunk) {
    if (state_ != ParseState::NeedMore) {
        return state_;
    }
    buffer_.append(chunk);

    if (!headersComplete_) {
        const std::size_t headerEnd = buffer_.find("\r\n\r\n", searchOffset_);
        if (headerEnd == std::string::npos) {
            if (buffer_.size() > limits_.maxHeaderBytes) {
                return fail(413, "header section exceeds limit");
            }
            // The terminator may straddle two chunks, so rewind three bytes.
            searchOffset_ = buffer_.size() >= 3 ? buffer_.size() - 3 : 0;
            return ParseState::NeedMore;
        }
        if (headerEnd + 4 > limits_.maxHeaderBytes) {
            return fail(413, "header section exceeds limit");
        }

        const std::string_view head(buffer_.data(), headerEnd + 2);
        const std::size_t lineEnd = head.find("\r\n");
        if (!parseRequestLine(head.substr(0, lineEnd))) {
            return state_;
        }
        if (!parseHeaderLines(head.substr(lineEnd + 2))) {
            return state_;
        }
        if (!applyHeaders()) {
            return state_;
        }
        headersComplete_ = true;
        bodyOffset_ = headerEnd + 4;
    }

    if (buffer_.size() - bodyOffset_ < contentLength_) {
        return ParseState::NeedMore;
    }

    request_.body.assign(buffer_, bodyOffset_, contentLength_);
    messageEnd_ = bodyOffset_ + contentLength_;
    state_ = ParseState::Complete;
    return state_;
}

bool RequestParser::parseRequestLine(std::string_view line) {
    const std::size_t firstSpace = line.find(' ');
    if (firstSpace == std::string_view::npos) {
        fail(400, "malformed request line");
        return false;
    }
    const std::size_t secondSpace = line.find(' ', firstSpace + 1);
    if (secondSpace == std::string_view::npos) {
        fail(400, "malformed request line");
        return false;
    }

    const std::string_view method = line.substr(0, firstSpace);
    const std::string_view target = line.substr(firstSpace + 1, secondSpace - firstSpace - 1);
    const std::string_view version = line.substr(secondSpace + 1);

    if (method.empty() || !std::all_of(method.begin(), method.end(), isTokenChar)) {
        fail(400, "invalid method token");
        return false;
    }
    if (version != "HTTP/1.1" && version != "HTTP/1.0") {
        fail(505, "unsupported HTTP version");
        return false;
    }
    if (target.size() > limits_.maxTargetBytes) {
        fail(414, "request target too long");
        return false;
    }
    if (target.empty() || target.front() != '/') {
        // Absolute-form targets are only required of proxies, which this is not.
        fail(400, "unsupported request target form");
        return false;
    }

    request_.methodText.assign(method);
    request_.method = methodFromString(method);
    request_.target.assign(target);
    request_.version.assign(version);

    const std::size_t questionMark = target.find('?');
    const std::string_view rawPath = target.substr(0, questionMark);
    if (questionMark != std::string_view::npos) {
        request_.query = parseQueryString(target.substr(questionMark + 1));
    }

    std::string decodedPath;
    if (!percentDecode(rawPath, decodedPath, false)) {
        fail(400, "invalid percent-encoding in path");
        return false;
    }
    request_.path = normalisePath(decodedPath);

    if (request_.method == Method::Unknown) {
        fail(501, "method not implemented");
        return false;
    }
    return true;
}

bool RequestParser::parseHeaderLines(std::string_view block) {
    std::size_t start = 0;
    while (start < block.size()) {
        const std::size_t end = block.find("\r\n", start);
        if (end == std::string_view::npos) {
            break;
        }
        const std::string_view line = block.substr(start, end - start);
        start = end + 2;
        if (line.empty()) {
            continue;
        }
        if (line.front() == ' ' || line.front() == '\t') {
            fail(400, "obsolete header line folding");
            return false;
        }

        const std::size_t colon = line.find(':');
        if (colon == std::string_view::npos || colon == 0) {
            fail(400, "malformed header line");
            return false;
        }
        const std::string_view name = line.substr(0, colon);
        if (!std::all_of(name.begin(), name.end(), isTokenChar)) {
            fail(400, "invalid header name");
            return false;
        }

        const std::string value(trim(line.substr(colon + 1)));
        auto [it, inserted] = request_.headers.try_emplace(std::string(name), value);
        if (!inserted) {
            // Repeated headers are combined per RFC 7230 section 3.2.2.
            it->second += ',';
            it->second += value;
        }
    }
    return true;
}

bool RequestParser::applyHeaders() {
    if (request_.version == "HTTP/1.1" && request_.header("Host") == nullptr) {
        fail(400, "missing Host header");
        return false;
    }
    if (request_.header("Transfer-Encoding") != nullptr) {
        // Chunked bodies are not implemented; rejecting is better than silently
        // treating the chunk framing as body content.
        fail(501, "Transfer-Encoding is not supported");
        return false;
    }

    const std::string* length = request_.header("Content-Length");
    if (length == nullptr) {
        contentLength_ = 0;
        return true;
    }

    const std::string_view text = trim(*length);
    if (text.empty() || text.find(',') != std::string_view::npos) {
        fail(400, "invalid Content-Length");
        return false;
    }
    std::size_t value = 0;
    const auto* first = text.data();
    const auto* last = text.data() + text.size();
    const auto result = std::from_chars(first, last, value);
    if (result.ec != std::errc{} || result.ptr != last) {
        fail(400, "invalid Content-Length");
        return false;
    }
    if (value > limits_.maxBodyBytes) {
        fail(413, "request body exceeds limit");
        return false;
    }
    contentLength_ = value;
    return true;
}

std::string_view RequestParser::leftover() const {
    if (state_ != ParseState::Complete) {
        return {};
    }
    return std::string_view(buffer_).substr(messageEnd_);
}

}  // namespace hs
