#include "json/Json.hpp"

#include <cmath>
#include <sstream>

namespace hs::json {
namespace {

const std::string kEmptyString;
const Array kEmptyArray;
const Object kEmptyObject;

void appendEscaped(std::string& out, std::string_view text) {
    out += '"';
    for (const unsigned char c : text) {
        switch (c) {
            case '"': out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            default:
                if (c < 0x20) {
                    static const char* kHex = "0123456789abcdef";
                    out += "\\u00";
                    out += kHex[c >> 4];
                    out += kHex[c & 0x0F];
                } else {
                    out += static_cast<char>(c);
                }
        }
    }
    out += '"';
}

void appendNumber(std::string& out, double value) {
    // Integral values are very common in this API, and printing them without a
    // ".000000" tail keeps the payloads readable.
    if (std::isfinite(value) && value == static_cast<double>(static_cast<long long>(value))) {
        out += std::to_string(static_cast<long long>(value));
        return;
    }
    std::ostringstream stream;
    stream.precision(17);
    stream << value;
    out += stream.str();
}

void serializeInto(const Value& value, std::string& out) {
    if (value.isNull()) {
        out += "null";
    } else if (value.isBool()) {
        out += value.asBool() ? "true" : "false";
    } else if (value.isNumber()) {
        appendNumber(out, value.asNumber());
    } else if (value.isString()) {
        appendEscaped(out, value.asString());
    } else if (value.isArray()) {
        out += '[';
        bool first = true;
        for (const auto& item : value.asArray()) {
            if (!first) out += ',';
            first = false;
            serializeInto(item, out);
        }
        out += ']';
    } else {
        out += '{';
        bool first = true;
        for (const auto& [key, item] : value.asObject()) {
            if (!first) out += ',';
            first = false;
            appendEscaped(out, key);
            out += ':';
            serializeInto(item, out);
        }
        out += '}';
    }
}

class Parser {
public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::optional<Value> run(std::string& error) {
        skipWhitespace();
        auto value = parseValue();
        if (!value) {
            error = error_;
            return std::nullopt;
        }
        skipWhitespace();
        if (position_ != text_.size()) {
            error = "trailing characters after JSON value";
            return std::nullopt;
        }
        return value;
    }

private:
    std::optional<Value> parseValue() {
        if (depth_ > kMaxDepth) {
            return reject("nesting too deep");
        }
        if (position_ >= text_.size()) {
            return reject("unexpected end of input");
        }
        switch (text_[position_]) {
            case '{': return parseObject();
            case '[': return parseArray();
            case '"': {
                std::string out;
                if (!parseString(out)) return std::nullopt;
                return Value(std::move(out));
            }
            case 't': return parseLiteral("true", Value(true));
            case 'f': return parseLiteral("false", Value(false));
            case 'n': return parseLiteral("null", Value(nullptr));
            default: return parseNumber();
        }
    }

    std::optional<Value> parseObject() {
        ++position_;
        ++depth_;
        Object object;
        skipWhitespace();
        if (consume('}')) {
            --depth_;
            return Value(std::move(object));
        }
        while (true) {
            skipWhitespace();
            std::string key;
            if (!parseString(key)) return std::nullopt;
            skipWhitespace();
            if (!consume(':')) return reject("expected ':' after object key");
            skipWhitespace();
            auto value = parseValue();
            if (!value) return std::nullopt;
            object.emplace_back(std::move(key), std::move(*value));
            skipWhitespace();
            if (consume(',')) continue;
            if (consume('}')) break;
            return reject("expected ',' or '}' in object");
        }
        --depth_;
        return Value(std::move(object));
    }

    std::optional<Value> parseArray() {
        ++position_;
        ++depth_;
        Array array;
        skipWhitespace();
        if (consume(']')) {
            --depth_;
            return Value(std::move(array));
        }
        while (true) {
            skipWhitespace();
            auto value = parseValue();
            if (!value) return std::nullopt;
            array.push_back(std::move(*value));
            skipWhitespace();
            if (consume(',')) continue;
            if (consume(']')) break;
            return reject("expected ',' or ']' in array");
        }
        --depth_;
        return Value(std::move(array));
    }

    bool parseString(std::string& out) {
        if (!consume('"')) {
            reject("expected string");
            return false;
        }
        out.clear();
        while (position_ < text_.size()) {
            const char c = text_[position_++];
            if (c == '"') {
                return true;
            }
            if (c != '\\') {
                out += c;
                continue;
            }
            if (position_ >= text_.size()) break;
            const char escape = text_[position_++];
            switch (escape) {
                case '"': out += '"'; break;
                case '\\': out += '\\'; break;
                case '/': out += '/'; break;
                case 'b': out += '\b'; break;
                case 'f': out += '\f'; break;
                case 'n': out += '\n'; break;
                case 'r': out += '\r'; break;
                case 't': out += '\t'; break;
                case 'u': {
                    if (!parseUnicodeEscape(out)) return false;
                    break;
                }
                default:
                    reject("invalid escape sequence");
                    return false;
            }
        }
        reject("unterminated string");
        return false;
    }

    // Only the basic multilingual plane is handled; surrogate pairs are passed
    // through as the replacement character rather than being decoded.
    bool parseUnicodeEscape(std::string& out) {
        if (position_ + 4 > text_.size()) {
            reject("truncated \\u escape");
            return false;
        }
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            const char c = text_[position_++];
            code <<= 4;
            if (c >= '0' && c <= '9') code |= static_cast<unsigned>(c - '0');
            else if (c >= 'a' && c <= 'f') code |= static_cast<unsigned>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') code |= static_cast<unsigned>(c - 'A' + 10);
            else { reject("invalid \\u escape"); return false; }
        }
        if (code >= 0xD800 && code <= 0xDFFF) {
            out += "\xEF\xBF\xBD";
        } else if (code < 0x80) {
            out += static_cast<char>(code);
        } else if (code < 0x800) {
            out += static_cast<char>(0xC0 | (code >> 6));
            out += static_cast<char>(0x80 | (code & 0x3F));
        } else {
            out += static_cast<char>(0xE0 | (code >> 12));
            out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (code & 0x3F));
        }
        return true;
    }

    std::optional<Value> parseNumber() {
        const std::size_t start = position_;
        if (position_ < text_.size() && (text_[position_] == '-' || text_[position_] == '+')) {
            ++position_;
        }
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' || c == '-' || c == '+') {
                ++position_;
            } else {
                break;
            }
        }
        if (start == position_) {
            return reject("unexpected character");
        }
        try {
            const std::string number(text_.substr(start, position_ - start));
            std::size_t consumed = 0;
            const double value = std::stod(number, &consumed);
            if (consumed != number.size()) {
                return reject("invalid number");
            }
            return Value(value);
        } catch (const std::exception&) {
            return reject("invalid number");
        }
    }

    std::optional<Value> parseLiteral(std::string_view literal, Value value) {
        if (text_.compare(position_, literal.size(), literal) != 0) {
            return reject("invalid literal");
        }
        position_ += literal.size();
        return value;
    }

    std::optional<Value> reject(const char* message) {
        if (error_.empty()) {
            error_ = std::string(message) + " at offset " + std::to_string(position_);
        }
        return std::nullopt;
    }

    bool consume(char expected) {
        if (position_ < text_.size() && text_[position_] == expected) {
            ++position_;
            return true;
        }
        return false;
    }

    void skipWhitespace() {
        while (position_ < text_.size()) {
            const char c = text_[position_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
                ++position_;
            } else {
                break;
            }
        }
    }

    static constexpr int kMaxDepth = 64;
    std::string_view text_;
    std::size_t position_ = 0;
    int depth_ = 0;
    std::string error_;
};

}  // namespace

bool Value::asBool(bool fallback) const {
    const bool* value = std::get_if<bool>(&data_);
    return value != nullptr ? *value : fallback;
}

double Value::asNumber(double fallback) const {
    const double* value = std::get_if<double>(&data_);
    return value != nullptr ? *value : fallback;
}

const std::string& Value::asString() const {
    const std::string* value = std::get_if<std::string>(&data_);
    return value != nullptr ? *value : kEmptyString;
}

const Array& Value::asArray() const {
    const Array* value = std::get_if<Array>(&data_);
    return value != nullptr ? *value : kEmptyArray;
}

const Object& Value::asObject() const {
    const Object* value = std::get_if<Object>(&data_);
    return value != nullptr ? *value : kEmptyObject;
}

const Value* Value::find(std::string_view key) const {
    const Object* object = std::get_if<Object>(&data_);
    if (object == nullptr) {
        return nullptr;
    }
    for (const auto& entry : *object) {
        if (entry.first == key) {
            return &entry.second;
        }
    }
    return nullptr;
}

std::string serialize(const Value& value) {
    std::string out;
    serializeInto(value, out);
    return out;
}

std::optional<Value> parse(std::string_view text, std::string* error) {
    Parser parser(text);
    std::string message;
    auto result = parser.run(message);
    if (!result && error != nullptr) {
        *error = message;
    }
    return result;
}

}  // namespace hs::json
