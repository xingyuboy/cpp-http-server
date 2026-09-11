#pragma once

#include <map>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <string>
#include <vector>

namespace hs {

struct User {
    int id = 0;
    std::string name;
    std::string email;
};

// In-memory store guarded by a shared_mutex: reads (list/find) dominate this
// workload and can run concurrently, while writes take the exclusive lock.
class UserStore {
public:
    UserStore();

    std::vector<User> list() const;
    std::optional<User> find(int id) const;
    User create(std::string name, std::string email);
    std::optional<User> update(int id, std::string name, std::string email);
    bool remove(int id);
    std::size_t size() const;

private:
    mutable std::shared_mutex mutex_;
    std::map<int, User> users_;
    int nextId_ = 1;
};

}  // namespace hs
