#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"

namespace hs {

std::string_view mimeTypeForExtension(std::string_view extension);

class StaticFileHandler {
public:
    // Throws if the document root does not exist: failing at startup is better
    // than serving 404s for every request and wondering why.
    explicit StaticFileHandler(const std::filesystem::path& documentRoot);

    HttpResponse handle(const HttpRequest& request) const;

    const std::filesystem::path& root() const noexcept { return root_; }

private:
    // Returns an empty path when the target escapes the document root.
    std::filesystem::path resolve(std::string_view requestPath) const;

    std::filesystem::path root_;
};

}  // namespace hs
