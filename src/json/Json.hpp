#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace hs::json {

class Value;

using Array = std::vector<Value>;
// Objects keep insertion order so generated payloads are stable and diffable.
// Lookup is linear, which is fine for the handful of keys an API body carries.
using Object = std::vector<std::pair<std::string, Value>>;

class Value {
public:
    Value() : data_(nullptr) {}
    Value(std::nullptr_t) : data_(nullptr) {}
    Value(bool value) : data_(value) {}
    Value(double value) : data_(value) {}
    Value(int value) : data_(static_cast<double>(value)) {}
    Value(const char* value) : data_(std::string(value)) {}
    Value(std::string value) : data_(std::move(value)) {}
    Value(Array value) : data_(std::move(value)) {}
    Value(Object value) : data_(std::move(value)) {}

    bool isNull() const { return std::holds_alternative<std::nullptr_t>(data_); }
    bool isBool() const { return std::holds_alternative<bool>(data_); }
    bool isNumber() const { return std::holds_alternative<double>(data_); }
    bool isString() const { return std::holds_alternative<std::string>(data_); }
    bool isArray() const { return std::holds_alternative<Array>(data_); }
    bool isObject() const { return std::holds_alternative<Object>(data_); }

    bool asBool(bool fallback = false) const;
    double asNumber(double fallback = 0.0) const;
    const std::string& asString() const;
    const Array& asArray() const;
    const Object& asObject() const;

    // Null when the key is absent or this value is not an object.
    const Value* find(std::string_view key) const;

private:
    std::variant<std::nullptr_t, bool, double, std::string, Array, Object> data_;
};

std::string serialize(const Value& value);
std::optional<Value> parse(std::string_view text, std::string* error = nullptr);

}  // namespace hs::json
