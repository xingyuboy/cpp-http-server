#include "api/UserStore.hpp"

#include <utility>

namespace hs {

UserStore::UserStore() {
    create("Alice", "alice@example.com");
    create("Bob", "bob@example.com");
}

std::vector<User> UserStore::list() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<User> result;
    result.reserve(users_.size());
    for (const auto& [id, user] : users_) {
        result.push_back(user);
    }
    return result;
}

std::optional<User> UserStore::find(int id) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto it = users_.find(id);
    if (it == users_.end()) {
        return std::nullopt;
    }
    return it->second;
}

User UserStore::create(std::string name, std::string email) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    User user{nextId_++, std::move(name), std::move(email)};
    users_.emplace(user.id, user);
    return user;
}

std::optional<User> UserStore::update(int id, std::string name, std::string email) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    const auto it = users_.find(id);
    if (it == users_.end()) {
        return std::nullopt;
    }
    it->second.name = std::move(name);
    it->second.email = std::move(email);
    return it->second;
}

bool UserStore::remove(int id) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    return users_.erase(id) > 0;
}

std::size_t UserStore::size() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return users_.size();
}

}  // namespace hs
