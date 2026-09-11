#include "TestFramework.hpp"
#include "json/Json.hpp"

using namespace hs;

TEST(SerialisesNestedObject) {
    const json::Value value = json::Object{
        {"users", json::Array{json::Object{{"id", 1}, {"name", "Alice"}}}},
        {"count", 1},
    };
    // The raw literal is hoisted out of the macro: MSVC's legacy preprocessor
    // mis-tokenises R"(...)" when it appears as a macro argument.
    const std::string expected = R"({"users":[{"id":1,"name":"Alice"}],"count":1})";
    CHECK_EQ(json::serialize(value), expected);
}

TEST(EscapesControlCharactersAndQuotes) {
    const json::Value value = json::Object{{"text", std::string("a\"b\\c\nd")}};
    const std::string expected = R"({"text":"a\"b\\c\nd"})";
    CHECK_EQ(json::serialize(value), expected);
}

TEST(ParsesObjectAndLooksUpFields) {
    std::string error;
    const auto value = json::parse(R"({"name":"Bob","age":30,"admin":true,"tags":["a","b"]})",
                                   &error);
    REQUIRE(value.has_value());
    CHECK(value->isObject());

    const json::Value* name = value->find("name");
    REQUIRE(name != nullptr);
    CHECK_EQ(name->asString(), std::string("Bob"));

    CHECK_EQ(value->find("age")->asNumber(), 30.0);
    CHECK(value->find("admin")->asBool());
    CHECK_EQ(value->find("tags")->asArray().size(), std::size_t(2));
    CHECK(value->find("missing") == nullptr);
}

TEST(RoundTripsThroughParseAndSerialize) {
    const std::string original = R"({"a":[1,2,3],"b":{"c":null,"d":false}})";
    const auto value = json::parse(original);
    REQUIRE(value.has_value());
    CHECK_EQ(json::serialize(*value), original);
}

TEST(ParsesEscapeSequences) {
    const auto value = json::parse(R"({"s":"line\nbreak\tand \u00e9"})");
    REQUIRE(value.has_value());
    CHECK_EQ(value->find("s")->asString(), std::string("line\nbreak\tand \xc3\xa9"));
}

TEST(RejectsMalformedJson) {
    CHECK(!json::parse("{").has_value());
    CHECK(!json::parse("{\"a\":}").has_value());
    CHECK(!json::parse("[1,2").has_value());
    CHECK(!json::parse("{\"a\":1}extra").has_value());
    CHECK(!json::parse("").has_value());
}

TEST(ReportsParseErrorText) {
    std::string error;
    CHECK(!json::parse("{\"a\" 1}", &error).has_value());
    CHECK(!error.empty());
}

TEST(AccessorsFallBackOnTypeMismatch) {
    const auto value = json::parse(R"({"n":5})");
    REQUIRE(value.has_value());
    CHECK_EQ(value->find("n")->asString(), std::string(""));
    CHECK_EQ(value->find("n")->asBool(true), true);
}
