#pragma once

#include "api/UserStore.hpp"
#include "routing/Router.hpp"

namespace hs {

// Owns no networking state: handlers take an HttpRequest and return an
// HttpResponse, which is what lets the API be tested without a socket.
class UserController {
public:
    explicit UserController(UserStore& store) : store_(store) {}

    void registerRoutes(Router& router);

    HttpResponse listUsers(const HttpRequest& request) const;
    HttpResponse getUser(const HttpRequest& request) const;
    HttpResponse createUser(const HttpRequest& request) const;
    HttpResponse replaceUser(const HttpRequest& request) const;
    HttpResponse deleteUser(const HttpRequest& request) const;

private:
    UserStore& store_;
};

}  // namespace hs
