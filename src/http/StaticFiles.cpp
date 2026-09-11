#include "http/StaticFiles.hpp"

#include <fstream>
#include <system_error>
#include <unordered_map>

#include "logging/Logger.hpp"

namespace hs {
namespace {

constexpr std::uintmax_t kMaxFileBytes = 32 * 1024 * 1024;

std::string lowercase(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

}  // namespace

std::string_view mimeTypeForExtension(std::string_view extension) {
    static const std::unordered_map<std::string, std::string_view> kTypes = {
        {".html", "text/html; charset=utf-8"},
        {".htm", "text/html; charset=utf-8"},
        {".css", "text/css; charset=utf-8"},
        {".js", "application/javascript; charset=utf-8"},
        {".mjs", "application/javascript; charset=utf-8"},
        {".json", "application/json"},
        {".txt", "text/plain; charset=utf-8"},
        {".md", "text/markdown; charset=utf-8"},
        {".xml", "application/xml"},
        {".csv", "text/csv; charset=utf-8"},
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".svg", "image/svg+xml"},
        {".ico", "image/x-icon"},
        {".webp", "image/webp"},
        {".woff", "font/woff"},
        {".woff2", "font/woff2"},
        {".pdf", "application/pdf"},
        {".zip", "application/zip"},
    };

    const auto it = kTypes.find(lowercase(extension));
    return it == kTypes.end() ? std::string_view("application/octet-stream") : it->second;
}

StaticFileHandler::StaticFileHandler(const std::filesystem::path& documentRoot) {
    std::error_code error;
    root_ = std::filesystem::canonical(documentRoot, error);
    if (error) {
        throw std::runtime_error("document root is not usable: " + documentRoot.string() + " (" +
                                 error.message() + ")");
    }
}

std::filesystem::path StaticFileHandler::resolve(std::string_view requestPath) const {
    // The parser already removed dot segments, but the filesystem can still
    // reintroduce an escape through a symlink, so the canonical result is
    // re-checked against the root here.
    std::filesystem::path relative(requestPath.substr(1));
    std::filesystem::path candidate = root_ / relative;

    std::error_code error;
    std::filesystem::path resolved = std::filesystem::weakly_canonical(candidate, error);
    if (error) {
        return {};
    }

    const auto rootText = root_.native();
    const auto resolvedText = resolved.native();
    if (resolvedText.size() < rootText.size() ||
        resolvedText.compare(0, rootText.size(), rootText) != 0) {
        return {};
    }
    if (resolvedText.size() > rootText.size() &&
        resolvedText[rootText.size()] != std::filesystem::path::preferred_separator) {
        return {};  // e.g. root "/srv/www" must not match "/srv/wwwroot"
    }
    return resolved;
}

HttpResponse StaticFileHandler::handle(const HttpRequest& request) const {
    if (request.method != Method::Get && request.method != Method::Head) {
        HttpResponse response = HttpResponse::error(405, "Static files support GET and HEAD only.");
        response.setHeader("Allow", "GET, HEAD");
        return response;
    }

    std::filesystem::path target = resolve(request.path);
    if (target.empty()) {
        LOG_WARN("rejected path outside document root: " << request.path);
        return HttpResponse::error(403, "Path is outside the document root.");
    }

    std::error_code error;
    if (std::filesystem::is_directory(target, error)) {
        target /= "index.html";
    } else if (!std::filesystem::exists(target, error) && !target.has_extension()) {
        // Clean URLs: /about serves about.html when it exists.
        std::filesystem::path withHtml = target;
        withHtml += ".html";
        if (std::filesystem::is_regular_file(withHtml, error)) {
            target = withHtml;
        }
    }

    if (!std::filesystem::is_regular_file(target, error)) {
        return HttpResponse::error(404, "No file matches " + request.path + ".");
    }

    const std::uintmax_t size = std::filesystem::file_size(target, error);
    if (error) {
        return HttpResponse::error(500, "Cannot stat the requested file.");
    }
    if (size > kMaxFileBytes) {
        LOG_WARN("refusing to serve oversized file " << target.string() << " (" << size << " bytes)");
        return HttpResponse::error(413, "File is larger than the server will serve.");
    }

    std::ifstream file(target, std::ios::binary);
    if (!file) {
        return HttpResponse::error(500, "Cannot open the requested file.");
    }

    std::string contents(static_cast<std::size_t>(size), '\0');
    file.read(contents.data(), static_cast<std::streamsize>(size));
    if (file.bad()) {
        return HttpResponse::error(500, "Error while reading the file.");
    }
    contents.resize(static_cast<std::size_t>(file.gcount()));

    HttpResponse response(200);
    response.setBody(std::move(contents));
    response.setHeader("Content-Type", std::string(mimeTypeForExtension(target.extension().string())));
    response.setHeader("Cache-Control", "no-cache");
    return response;
}

}  // namespace hs
