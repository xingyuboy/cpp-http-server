#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace hs {

std::string_view reasonPhrase(int status);

class HttpResponse {
public:
    HttpResponse() = default;
    explicit HttpResponse(int status) : status_(status) {}

    static HttpResponse text(int status, std::string body);
    static HttpResponse json(int status, std::string body);
    static HttpResponse noContent(int status = 204);
    // Body is a short HTML page so that a browser shows something readable.
    static HttpResponse error(int status, std::string_view detail = {});

    int status() const noexcept { return status_; }
    void setStatus(int status) noexcept { status_ = status; }

    const std::string& body() const noexcept { return body_; }
    void setBody(std::string body) { body_ = std::move(body); }

    void setHeader(std::string name, std::string value);
    void addHeader(std::string name, std::string value);
    const std::string* header(std::string_view name) const;

    // HEAD responses keep Content-Length but must not carry a body.
    std::string serialize(bool includeBody = true) const;

private:
    int status_ = 200;
    std::vector<std::pair<std::string, std::string>> headers_;
    std::string body_;
};

}  // namespace hs
