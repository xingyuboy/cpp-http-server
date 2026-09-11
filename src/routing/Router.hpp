#pragma once

#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "http/HttpRequest.hpp"
#include "http/HttpResponse.hpp"

namespace hs {

using RouteHandler = std::function<HttpResponse(const HttpRequest&)>;

// Patterns are plain paths with optional "{name}" segments, e.g.
// "/api/users/{id}". Matching is a linear scan over the registered routes,
// which is more than fast enough for a route table of this size and far easier
// to follow than a trie.
class Router {
public:
    void add(Method method, std::string_view pattern, RouteHandler handler);
    void get(std::string_view pattern, RouteHandler handler);
    void post(std::string_view pattern, RouteHandler handler);
    void put(std::string_view pattern, RouteHandler handler);
    void del(std::string_view pattern, RouteHandler handler);

    // Called when no route matches; the static file handler is installed here.
    void setFallback(RouteHandler handler);

    HttpResponse dispatch(HttpRequest& request) const;

    std::size_t routeCount() const noexcept { return routes_.size(); }

private:
    struct Segment {
        std::string text;
        bool isParameter = false;
    };

    struct Route {
        Method method;
        std::string pattern;
        std::vector<Segment> segments;
        RouteHandler handler;
    };

    static std::vector<Segment> compile(std::string_view pattern);
    static bool matches(const std::vector<Segment>& segments, std::string_view path,
                        std::vector<std::pair<std::string, std::string>>& captures);

    std::vector<Route> routes_;
    RouteHandler fallback_;
};

}  // namespace hs
