#include "routing/Router.hpp"

#include <set>
#include <stdexcept>

namespace hs {

std::vector<Router::Segment> Router::compile(std::string_view pattern) {
    if (pattern.empty() || pattern.front() != '/') {
        throw std::invalid_argument("route pattern must start with '/': " + std::string(pattern));
    }

    std::vector<Segment> segments;
    std::size_t start = 1;
    while (start <= pattern.size()) {
        const std::size_t end = pattern.find('/', start);
        const std::string_view raw =
            pattern.substr(start, end == std::string_view::npos ? std::string_view::npos
                                                                : end - start);
        if (!raw.empty()) {
            Segment segment;
            if (raw.size() > 2 && raw.front() == '{' && raw.back() == '}') {
                segment.isParameter = true;
                segment.text = std::string(raw.substr(1, raw.size() - 2));
            } else {
                segment.text = std::string(raw);
            }
            segments.push_back(std::move(segment));
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }
    return segments;
}

bool Router::matches(const std::vector<Segment>& segments, std::string_view path,
                     std::vector<std::pair<std::string, std::string>>& captures) {
    captures.clear();
    std::size_t index = 0;
    std::size_t start = 1;  // paths are normalised, so they always start with '/'

    while (start <= path.size()) {
        const std::size_t end = path.find('/', start);
        const std::string_view raw =
            path.substr(start, end == std::string_view::npos ? std::string_view::npos : end - start);
        if (!raw.empty()) {
            if (index >= segments.size()) {
                return false;
            }
            const Segment& segment = segments[index];
            if (segment.isParameter) {
                captures.emplace_back(segment.text, std::string(raw));
            } else if (segment.text != raw) {
                return false;
            }
            ++index;
        }
        if (end == std::string_view::npos) {
            break;
        }
        start = end + 1;
    }

    return index == segments.size();
}

void Router::add(Method method, std::string_view pattern, RouteHandler handler) {
    routes_.push_back(Route{method, std::string(pattern), compile(pattern), std::move(handler)});
}

void Router::get(std::string_view pattern, RouteHandler handler) {
    add(Method::Get, pattern, std::move(handler));
}

void Router::post(std::string_view pattern, RouteHandler handler) {
    add(Method::Post, pattern, std::move(handler));
}

void Router::put(std::string_view pattern, RouteHandler handler) {
    add(Method::Put, pattern, std::move(handler));
}

void Router::del(std::string_view pattern, RouteHandler handler) {
    add(Method::Delete, pattern, std::move(handler));
}

void Router::setFallback(RouteHandler handler) {
    fallback_ = std::move(handler);
}

HttpResponse Router::dispatch(HttpRequest& request) const {
    // HEAD must route exactly like GET; the connection layer drops the body.
    const Method effective = request.method == Method::Head ? Method::Get : request.method;

    std::vector<std::pair<std::string, std::string>> captures;
    std::set<std::string> allowed;

    for (const Route& route : routes_) {
        if (!matches(route.segments, request.path, captures)) {
            continue;
        }
        if (route.method != effective) {
            allowed.insert(std::string(methodName(route.method)));
            continue;
        }
        request.params.clear();
        for (auto& [name, value] : captures) {
            request.params.emplace(std::move(name), std::move(value));
        }
        return route.handler(request);
    }

    if (!allowed.empty()) {
        allowed.insert("HEAD");
        std::string allowHeader;
        for (const auto& method : allowed) {
            if (!allowHeader.empty()) allowHeader += ", ";
            allowHeader += method;
        }
        HttpResponse response = HttpResponse::error(405, "This path does not accept " +
                                                             request.methodText + ".");
        response.setHeader("Allow", allowHeader);
        return response;
    }

    if (fallback_) {
        return fallback_(request);
    }
    return HttpResponse::error(404, "No route matched " + request.path + ".");
}

}  // namespace hs
