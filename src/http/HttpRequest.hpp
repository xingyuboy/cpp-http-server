#pragma once

#include <map>
#include <string>
#include <string_view>
#include <unordered_map>

namespace hs {

enum class Method { Get, Post, Put, Delete, Head, Unknown };

std::string_view methodName(Method method);
Method methodFromString(std::string_view text);

struct CaseInsensitiveLess {
    using is_transparent = void;
    bool operator()(std::string_view lhs, std::string_view rhs) const;
};

// Header names are case-insensitive per RFC 7230, hence the custom comparator
// rather than a plain map.
using HeaderMap = std::map<std::string, std::string, CaseInsensitiveLess>;

struct HttpRequest {
    Method method = Method::Unknown;
    std::string methodText;
    std::string target;   // original request target, including the query string
    std::string path;     // percent-decoded and dot-segment normalised
    std::string version = "HTTP/1.1";
    HeaderMap headers;
    std::map<std::string, std::string> query;
    std::unordered_map<std::string, std::string> params;  // filled in by the router
    std::string body;

    const std::string* header(std::string_view name) const;
    std::string queryValue(std::string_view name, std::string fallback = {}) const;
    std::string param(std::string_view name, std::string fallback = {}) const;
    bool keepAlive() const;
};

}  // namespace hs
