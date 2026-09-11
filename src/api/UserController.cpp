#include "api/UserController.hpp"

#include <charconv>

#include "json/Json.hpp"

namespace hs {
namespace {

json::Value toJson(const User& user) {
    return json::Object{{"id", user.id}, {"name", user.name}, {"email", user.email}};
}

HttpResponse jsonError(int status, std::string message) {
    return HttpResponse::json(status,
                              json::serialize(json::Object{{"error", std::move(message)}}));
}

bool parseId(std::string_view text, int& out) {
    const auto* last = text.data() + text.size();
    const auto result = std::from_chars(text.data(), last, out);
    return result.ec == std::errc{} && result.ptr == last;
}

// Shared by POST and PUT: both take the same body shape.
struct UserInput {
    std::string name;
    std::string email;
};

bool readUserInput(const HttpRequest& request, UserInput& out, std::string& error) {
    const std::string* contentType = request.header("Content-Type");
    if (contentType != nullptr && contentType->find("application/json") == std::string::npos) {
        error = "Content-Type must be application/json";
        return false;
    }

    std::string parseError;
    auto body = json::parse(request.body, &parseError);
    if (!body) {
        error = "invalid JSON body: " + parseError;
        return false;
    }
    const json::Value* name = body->find("name");
    if (name == nullptr || !name->isString() || name->asString().empty()) {
        error = "field 'name' is required and must be a non-empty string";
        return false;
    }
    const json::Value* email = body->find("email");
    if (email != nullptr && !email->isString()) {
        error = "field 'email' must be a string";
        return false;
    }
    out.name = name->asString();
    out.email = email != nullptr ? email->asString() : std::string();
    return true;
}

}  // namespace

void UserController::registerRoutes(Router& router) {
    router.get("/api/users", [this](const HttpRequest& request) { return listUsers(request); });
    router.get("/api/users/{id}", [this](const HttpRequest& request) { return getUser(request); });
    router.post("/api/users", [this](const HttpRequest& request) { return createUser(request); });
    router.put("/api/users/{id}",
               [this](const HttpRequest& request) { return replaceUser(request); });
    router.del("/api/users/{id}",
               [this](const HttpRequest& request) { return deleteUser(request); });
}

HttpResponse UserController::listUsers(const HttpRequest& request) const {
    const std::string filter = request.queryValue("name");

    json::Array users;
    for (const User& user : store_.list()) {
        if (!filter.empty() && user.name.find(filter) == std::string::npos) {
            continue;
        }
        users.push_back(toJson(user));
    }

    const auto count = static_cast<int>(users.size());
    return HttpResponse::json(
        200, json::serialize(json::Object{{"users", std::move(users)}, {"count", count}}));
}

HttpResponse UserController::getUser(const HttpRequest& request) const {
    int id = 0;
    if (!parseId(request.param("id"), id)) {
        return jsonError(400, "user id must be an integer");
    }
    const auto user = store_.find(id);
    if (!user) {
        return jsonError(404, "no user with id " + std::to_string(id));
    }
    return HttpResponse::json(200, json::serialize(toJson(*user)));
}

HttpResponse UserController::createUser(const HttpRequest& request) const {
    UserInput input;
    std::string error;
    if (!readUserInput(request, input, error)) {
        return jsonError(400, std::move(error));
    }

    const User user = store_.create(std::move(input.name), std::move(input.email));
    HttpResponse response = HttpResponse::json(201, json::serialize(toJson(user)));
    response.setHeader("Location", "/api/users/" + std::to_string(user.id));
    return response;
}

HttpResponse UserController::replaceUser(const HttpRequest& request) const {
    int id = 0;
    if (!parseId(request.param("id"), id)) {
        return jsonError(400, "user id must be an integer");
    }
    UserInput input;
    std::string error;
    if (!readUserInput(request, input, error)) {
        return jsonError(400, std::move(error));
    }

    const auto user = store_.update(id, std::move(input.name), std::move(input.email));
    if (!user) {
        return jsonError(404, "no user with id " + std::to_string(id));
    }
    return HttpResponse::json(200, json::serialize(toJson(*user)));
}

HttpResponse UserController::deleteUser(const HttpRequest& request) const {
    int id = 0;
    if (!parseId(request.param("id"), id)) {
        return jsonError(400, "user id must be an integer");
    }
    if (!store_.remove(id)) {
        return jsonError(404, "no user with id " + std::to_string(id));
    }
    return HttpResponse::noContent();
}

}  // namespace hs
