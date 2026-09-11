#include "TestFramework.hpp"
#include "api/UserController.hpp"
#include "api/UserStore.hpp"
#include "json/Json.hpp"
#include "routing/Router.hpp"

using namespace hs;

namespace {

struct ApiFixture {
    UserStore store;
    UserController controller{store};
    Router router;

    ApiFixture() { controller.registerRoutes(router); }

    HttpResponse call(Method method, const std::string& path, const std::string& body = {}) {
        HttpRequest request;
        request.method = method;
        request.methodText = std::string(methodName(method));
        request.path = path;
        request.target = path;
        request.body = body;
        if (!body.empty()) {
            request.headers.emplace("Content-Type", "application/json");
        }
        return router.dispatch(request);
    }
};

}  // namespace

TEST(ListsSeededUsers) {
    ApiFixture api;
    const HttpResponse response = api.call(Method::Get, "/api/users");
    CHECK_EQ(response.status(), 200);

    const auto body = json::parse(response.body());
    REQUIRE(body.has_value());
    CHECK_EQ(body->find("count")->asNumber(), 2.0);
    CHECK_EQ(body->find("users")->asArray().size(), std::size_t(2));
    CHECK_EQ(body->find("users")->asArray()[0].find("name")->asString(), std::string("Alice"));
}

TEST(GetsSingleUserById) {
    ApiFixture api;
    const HttpResponse response = api.call(Method::Get, "/api/users/1");
    CHECK_EQ(response.status(), 200);

    const auto body = json::parse(response.body());
    REQUIRE(body.has_value());
    CHECK_EQ(body->find("id")->asNumber(), 1.0);
    CHECK_EQ(body->find("name")->asString(), std::string("Alice"));
}

TEST(UnknownUserReturns404) {
    ApiFixture api;
    CHECK_EQ(api.call(Method::Get, "/api/users/999").status(), 404);
}

TEST(NonNumericIdReturns400) {
    ApiFixture api;
    CHECK_EQ(api.call(Method::Get, "/api/users/abc").status(), 400);
}

TEST(CreatesUserAndReturnsLocation) {
    ApiFixture api;
    const HttpResponse response =
        api.call(Method::Post, "/api/users", R"({"name":"Carol","email":"carol@example.com"})");
    CHECK_EQ(response.status(), 201);

    const std::string* location = response.header("Location");
    REQUIRE(location != nullptr);
    CHECK_EQ(*location, std::string("/api/users/3"));

    const auto body = json::parse(response.body());
    REQUIRE(body.has_value());
    CHECK_EQ(body->find("name")->asString(), std::string("Carol"));
    CHECK_EQ(api.store.size(), std::size_t(3));
}

TEST(CreateRejectsMissingName) {
    ApiFixture api;
    const HttpResponse response = api.call(Method::Post, "/api/users", R"({"email":"x@y.z"})");
    CHECK_EQ(response.status(), 400);
    CHECK(json::parse(response.body())->find("error") != nullptr);
}

TEST(CreateRejectsInvalidJson) {
    ApiFixture api;
    CHECK_EQ(api.call(Method::Post, "/api/users", "{not json").status(), 400);
}

TEST(UpdatesExistingUser) {
    ApiFixture api;
    const HttpResponse response =
        api.call(Method::Put, "/api/users/2", R"({"name":"Bobby","email":"bobby@example.com"})");
    CHECK_EQ(response.status(), 200);
    CHECK_EQ(api.store.find(2)->name, std::string("Bobby"));
}

TEST(UpdateOfMissingUserReturns404) {
    ApiFixture api;
    const std::string payload = R"({"name":"Ghost"})";
    CHECK_EQ(api.call(Method::Put, "/api/users/77", payload).status(), 404);
}

TEST(DeleteRemovesUserAndReturns204) {
    ApiFixture api;
    CHECK_EQ(api.call(Method::Delete, "/api/users/1").status(), 204);
    CHECK(!api.store.find(1).has_value());
    CHECK_EQ(api.call(Method::Delete, "/api/users/1").status(), 404);
}

TEST(FiltersUserListByNameQuery) {
    ApiFixture api;
    HttpRequest request;
    request.method = Method::Get;
    request.methodText = "GET";
    request.path = "/api/users";
    request.query.emplace("name", "Ali");

    const HttpResponse response = api.router.dispatch(request);
    const auto body = json::parse(response.body());
    REQUIRE(body.has_value());
    CHECK_EQ(body->find("count")->asNumber(), 1.0);
}

TEST(StoreAssignsIncreasingIds) {
    UserStore store;
    const User first = store.create("X", "x@example.com");
    const User second = store.create("Y", "y@example.com");
    CHECK(second.id > first.id);
}
