#include "http/HttpRequest.hpp"

#include <algorithm>
#include <cctype>

namespace hs {
namespace {

char lowerChar(char c) {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

bool equalsIgnoreCase(std::string_view lhs, std::string_view rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(),
                      [](char a, char b) { return lowerChar(a) == lowerChar(b); });
}

}  // namespace

bool CaseInsensitiveLess::operator()(std::string_view lhs, std::string_view rhs) const {
    return std::lexicographical_compare(
        lhs.begin(), lhs.end(), rhs.begin(), rhs.end(),
        [](char a, char b) { return lowerChar(a) < lowerChar(b); });
}

std::string_view methodName(Method method) {
    switch (method) {
        case Method::Get: return "GET";
        case Method::Post: return "POST";
        case Method::Put: return "PUT";
        case Method::Delete: return "DELETE";
        case Method::Head: return "HEAD";
        case Method::Unknown: break;
    }
    return "UNKNOWN";
}

Method methodFromString(std::string_view text) {
    if (text == "GET") return Method::Get;
    if (text == "POST") return Method::Post;
    if (text == "PUT") return Method::Put;
    if (text == "DELETE") return Method::Delete;
    if (text == "HEAD") return Method::Head;
    return Method::Unknown;
}

const std::string* HttpRequest::header(std::string_view name) const {
    const auto it = headers.find(name);
    return it == headers.end() ? nullptr : &it->second;
}

std::string HttpRequest::queryValue(std::string_view name, std::string fallback) const {
    const auto it = query.find(std::string(name));
    return it == query.end() ? std::move(fallback) : it->second;
}

std::string HttpRequest::param(std::string_view name, std::string fallback) const {
    const auto it = params.find(std::string(name));
    return it == params.end() ? std::move(fallback) : it->second;
}

bool HttpRequest::keepAlive() const {
    const std::string* connection = header("Connection");
    if (connection != nullptr) {
        if (equalsIgnoreCase(*connection, "close")) return false;
        if (equalsIgnoreCase(*connection, "keep-alive")) return true;
    }
    // HTTP/1.1 defaults to persistent connections, HTTP/1.0 does not.
    return version == "HTTP/1.1";
}

}  // namespace hs
