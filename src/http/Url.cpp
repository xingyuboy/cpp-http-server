#include "http/Url.hpp"

#include <cctype>
#include <vector>

namespace hs {
namespace {

int hexValue(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

}  // namespace

bool percentDecode(std::string_view input, std::string& out, bool plusIsSpace) {
    out.clear();
    out.reserve(input.size());
    for (std::size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '%') {
            if (i + 2 >= input.size()) {
                return false;
            }
            const int high = hexValue(input[i + 1]);
            const int low = hexValue(input[i + 2]);
            if (high < 0 || low < 0) {
                return false;
            }
            const char decoded = static_cast<char>(high * 16 + low);
            if (decoded == '\0') {
                return false;
            }
            out += decoded;
            i += 2;
        } else if (c == '+' && plusIsSpace) {
            out += ' ';
        } else {
            out += c;
        }
    }
    return true;
}

std::string normalisePath(std::string_view path) {
    std::vector<std::string_view> segments;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view segment =
            path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (segment == "..") {
            if (!segments.empty()) {
                segments.pop_back();
            }
        } else if (!segment.empty() && segment != ".") {
            segments.push_back(segment);
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    std::string result;
    for (const auto& segment : segments) {
        result += '/';
        result += segment;
    }
    if (result.empty()) {
        result = "/";
    } else if (path.size() > 1 && path.back() == '/') {
        result += '/';
    }
    return result;
}

std::map<std::string, std::string> parseQueryString(std::string_view query) {
    std::map<std::string, std::string> result;
    std::size_t start = 0;
    while (start < query.size()) {
        std::size_t end = query.find('&', start);
        if (end == std::string_view::npos) {
            end = query.size();
        }
        const std::string_view pair = query.substr(start, end - start);
        if (!pair.empty()) {
            const std::size_t equals = pair.find('=');
            std::string key;
            std::string value;
            const bool keyOk = percentDecode(
                equals == std::string_view::npos ? pair : pair.substr(0, equals), key, true);
            const bool valueOk =
                equals == std::string_view::npos
                    ? true
                    : percentDecode(pair.substr(equals + 1), value, true);
            // A single malformed pair should not discard the whole query string.
            if (keyOk && valueOk && !key.empty()) {
                result.emplace(std::move(key), std::move(value));
            }
        }
        start = end + 1;
    }
    return result;
}

}  // namespace hs
