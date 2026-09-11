#include "TestFramework.hpp"
#include "http/HttpParser.hpp"
#include "routing/Router.hpp"

using namespace hs;

namespace {

HttpRequest makeRequest(Method method, const std::string& path) {
    HttpRequest request;
    request.method = method;
    request.methodText = std::string(methodName(method));
    request.path = path;
    request.target = path;
    return request;
}

Router makeRouter() {
    Router router;
    router.get("/", [](const HttpRequest&) { return HttpResponse::text(200, "home"); });
    router.get("/api/users", [](const HttpRequest&) { return HttpResponse::text(200, "list"); });
    router.get("/api/users/{id}", [](const HttpRequest& request) {
        return HttpResponse::text(200, "user:" + request.param("id"));
    });
    router.get("/api/users/{id}/posts/{postId}", [](const HttpRequest& request) {
        return HttpResponse::text(200, request.param("id") + "/" + request.param("postId"));
    });
    router.post("/api/users", [](const HttpRequest&) { return HttpResponse::text(201, "created"); });
    router.del("/api/users/{id}", [](const HttpRequest&) { return HttpResponse::noContent(); });
    return router;
}

}  // namespace

TEST(RoutesStaticPaths) {
    Router router = makeRouter();
    HttpRequest request = makeRequest(Method::Get, "/api/users");
    const HttpResponse response = router.dispatch(request);
    CHECK_EQ(response.status(), 200);
    CHECK_EQ(response.body(), std::string("list"));
}

TEST(RoutesRootPath) {
    Router router = makeRouter();
    HttpRequest request = makeRequest(Method::Get, "/");
    CHECK_EQ(router.dispatch(request).body(), std::string("home"));
}

TEST(CapturesSingleRouteParameter) {
    Router router = makeRouter();
    HttpRequest request = makeRequest(Method::Get, "/api/users/42");
    const HttpResponse response = router.dispatch(request);
    CHECK_EQ(response.body(), std::string("user:42"));
    CHECK_EQ(request.param("id"), std::string("42"));
}

TEST(CapturesMultipleRouteParameters) {
    Router router = makeRouter();
    HttpRequest request = makeRequest(Method::Get, "/api/users/7/posts/13");
    CHECK_EQ(router.dispatch(request).body(), std::string("7/13"));
}

TEST(SameMethodDifferentPathsDoNotCollide) {
    Router router = makeRouter();
    HttpRequest post = makeRequest(Method::Post, "/api/users");
    CHECK_EQ(router.dispatch(post).status(), 201);
}

TEST(MethodMismatchReturns405WithAllowHeader) {
    Router router = makeRouter();
    HttpRequest request = makeRequest(Method::Put, "/api/users/1");
    const HttpResponse response = router.dispatch(request);
    CHECK_EQ(response.status(), 405);

    const std::string* allow = response.header("Allow");
    REQUIRE(allow != nullptr);
    CHECK(allow->find("GET") != std::string::npos);
    CHECK(allow->find("DELETE") != std::string::npos);
}

TEST(UnmatchedPathReturns404WithoutFallback) {
    Router router = makeRouter();
    HttpRequest request = makeRequest(Method::Get, "/nothing/here");
    CHECK_EQ(router.dispatch(request).status(), 404);
}

TEST(FallbackHandlesUnmatchedPaths) {
    Router router = makeRouter();
    router.setFallback([](const HttpRequest&) { return HttpResponse::text(200, "static"); });
    HttpRequest request = makeRequest(Method::Get, "/style.css");
    CHECK_EQ(router.dispatch(request).body(), std::string("static"));
}

TEST(HeadIsRoutedLikeGet) {
    Router router = makeRouter();
    HttpRequest request = makeRequest(Method::Head, "/api/users");
    const HttpResponse response = router.dispatch(request);
    CHECK_EQ(response.status(), 200);
    CHECK_EQ(response.body(), std::string("list"));
}

TEST(ParameterRouteDoesNotMatchExtraSegments) {
    Router router = makeRouter();
    HttpRequest request = makeRequest(Method::Get, "/api/users/1/extra");
    CHECK_EQ(router.dispatch(request).status(), 404);
}

TEST(TrailingSlashMatchesSameRoute) {
    Router router = makeRouter();
    HttpRequest request = makeRequest(Method::Get, "/api/users/");
    CHECK_EQ(router.dispatch(request).status(), 200);
}

TEST(RejectsPatternWithoutLeadingSlash) {
    Router router;
    bool threw = false;
    try {
        router.get("api/users", [](const HttpRequest&) { return HttpResponse::text(200, ""); });
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    CHECK(threw);
}
