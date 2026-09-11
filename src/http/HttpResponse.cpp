#include "http/HttpResponse.hpp"

#include <algorithm>
#include <cctype>

namespace hs {
namespace {

bool sameHeaderName(std::string_view lhs, std::string_view rhs) {
    return lhs.size() == rhs.size() &&
           std::equal(lhs.begin(), lhs.end(), rhs.begin(), [](char a, char b) {
               return std::tolower(static_cast<unsigned char>(a)) ==
                      std::tolower(static_cast<unsigned char>(b));
           });
}

std::string escapeHtml(std::string_view input) {
    std::string out;
    out.reserve(input.size());
    for (const char c : input) {
        switch (c) {
            case '<': out += "&lt;"; break;
            case '>': out += "&gt;"; break;
            case '&': out += "&amp;"; break;
            case '"': out += "&quot;"; break;
            default: out += c; break;
        }
    }
    return out;
}

}  // namespace

std::string_view reasonPhrase(int status) {
    switch (status) {
        case 200: return "OK";
        case 201: return "Created";
        case 204: return "No Content";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 408: return "Request Timeout";
        case 411: return "Length Required";
        case 413: return "Payload Too Large";
        case 414: return "URI Too Long";
        case 415: return "Unsupported Media Type";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 503: return "Service Unavailable";
        case 505: return "HTTP Version Not Supported";
        default: return "Unknown";
    }
}

HttpResponse HttpResponse::text(int status, std::string body) {
    HttpResponse response(status);
    response.setBody(std::move(body));
    response.setHeader("Content-Type", "text/plain; charset=utf-8");
    return response;
}

HttpResponse HttpResponse::json(int status, std::string body) {
    HttpResponse response(status);
    response.setBody(std::move(body));
    response.setHeader("Content-Type", "application/json");
    return response;
}

HttpResponse HttpResponse::noContent(int status) {
    return HttpResponse(status);
}

HttpResponse HttpResponse::error(int status, std::string_view detail) {
    const std::string_view reason = reasonPhrase(status);
    std::string body = "<!doctype html><title>" + std::to_string(status) + " " +
                       std::string(reason) + "</title><h1>" + std::to_string(status) + " " +
                       std::string(reason) + "</h1>";
    if (!detail.empty()) {
        body += "<p>" + escapeHtml(detail) + "</p>";
    }
    HttpResponse response(status);
    response.setBody(std::move(body));
    response.setHeader("Content-Type", "text/html; charset=utf-8");
    return response;
}

void HttpResponse::setHeader(std::string name, std::string value) {
    for (auto& entry : headers_) {
        if (sameHeaderName(entry.first, name)) {
            entry.second = std::move(value);
            return;
        }
    }
    headers_.emplace_back(std::move(name), std::move(value));
}

void HttpResponse::addHeader(std::string name, std::string value) {
    headers_.emplace_back(std::move(name), std::move(value));
}

const std::string* HttpResponse::header(std::string_view name) const {
    for (const auto& entry : headers_) {
        if (sameHeaderName(entry.first, name)) {
            return &entry.second;
        }
    }
    return nullptr;
}

std::string HttpResponse::serialize(bool includeBody) const {
    std::string out;
    out.reserve(body_.size() + 256);
    out += "HTTP/1.1 ";
    out += std::to_string(status_);
    out += ' ';
    out += reasonPhrase(status_);
    out += "\r\n";

    const bool bodyAllowed = status_ != 204 && status_ != 304;
    for (const auto& [name, value] : headers_) {
        if (sameHeaderName(name, "Content-Length")) {
            continue;  // recomputed below so it can never disagree with the body
        }
        out += name;
        out += ": ";
        out += value;
        out += "\r\n";
    }
    if (bodyAllowed) {
        out += "Content-Length: ";
        out += std::to_string(body_.size());
        out += "\r\n";
    }
    out += "\r\n";
    if (includeBody && bodyAllowed) {
        out += body_;
    }
    return out;
}

}  // namespace hs
