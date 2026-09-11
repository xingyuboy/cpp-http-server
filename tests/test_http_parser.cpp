#include "TestFramework.hpp"
#include "http/HttpParser.hpp"
#include "http/Url.hpp"

using namespace hs;

namespace {

RequestParser parseAll(const std::string& raw, ParserLimits limits = {}) {
    RequestParser parser(limits);
    parser.consume(raw);
    return parser;
}

}  // namespace

TEST(ParsesSimpleGetRequest) {
    auto parser = parseAll("GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n");
    REQUIRE(parser.state() == ParseState::Complete);

    const HttpRequest& request = parser.request();
    CHECK(request.method == Method::Get);
    CHECK_EQ(request.path, std::string("/index.html"));
    CHECK_EQ(request.version, std::string("HTTP/1.1"));
    CHECK(request.body.empty());
    CHECK(request.keepAlive());
}

TEST(HeaderLookupIsCaseInsensitive) {
    auto parser = parseAll("GET / HTTP/1.1\r\nHost: localhost\r\nX-Custom: value\r\n\r\n");
    REQUIRE(parser.state() == ParseState::Complete);
    const std::string* header = parser.request().header("x-CUSTOM");
    REQUIRE(header != nullptr);
    CHECK_EQ(*header, std::string("value"));
}

TEST(ParsesQueryParameters) {
    auto parser = parseAll("GET /search?q=hello+world&page=2&flag HTTP/1.1\r\nHost: x\r\n\r\n");
    REQUIRE(parser.state() == ParseState::Complete);

    const HttpRequest& request = parser.request();
    CHECK_EQ(request.path, std::string("/search"));
    CHECK_EQ(request.queryValue("q"), std::string("hello world"));
    CHECK_EQ(request.queryValue("page"), std::string("2"));
    CHECK_EQ(request.queryValue("flag"), std::string(""));
    CHECK_EQ(request.queryValue("missing", "default"), std::string("default"));
}

TEST(ParsesBodyUsingContentLength) {
    auto parser = parseAll(
        "POST /api/users HTTP/1.1\r\nHost: x\r\nContent-Length: 11\r\n\r\nhello world");
    REQUIRE(parser.state() == ParseState::Complete);
    CHECK(parser.request().method == Method::Post);
    CHECK_EQ(parser.request().body, std::string("hello world"));
}

TEST(WaitsForIncompleteRequest) {
    RequestParser parser;
    CHECK(parser.consume("GET / HTTP/1.1\r\nHo") == ParseState::NeedMore);
    CHECK(parser.consume("st: localhost\r\n") == ParseState::NeedMore);
    CHECK(parser.consume("\r\n") == ParseState::Complete);
}

TEST(WaitsForIncompleteBody) {
    RequestParser parser;
    CHECK(parser.consume("PUT /a HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\n\r\nab") ==
          ParseState::NeedMore);
    CHECK(parser.consume("cde") == ParseState::Complete);
    CHECK_EQ(parser.request().body, std::string("abcde"));
}

TEST(ExposesPipelinedLeftoverBytes) {
    auto parser = parseAll(
        "GET /one HTTP/1.1\r\nHost: x\r\n\r\nGET /two HTTP/1.1\r\nHost: x\r\n\r\n");
    REQUIRE(parser.state() == ParseState::Complete);
    CHECK_EQ(parser.request().path, std::string("/one"));

    RequestParser second;
    CHECK(second.consume(parser.leftover()) == ParseState::Complete);
    CHECK_EQ(second.request().path, std::string("/two"));
}

TEST(RejectsMalformedRequestLine) {
    auto parser = parseAll("GET\r\nHost: x\r\n\r\n");
    CHECK(parser.state() == ParseState::Failed);
    CHECK_EQ(parser.errorStatus(), 400);
}

TEST(RejectsUnknownMethodWith501) {
    auto parser = parseAll("PATCH /a HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(parser.state() == ParseState::Failed);
    CHECK_EQ(parser.errorStatus(), 501);
}

TEST(RejectsUnsupportedVersion) {
    auto parser = parseAll("GET / HTTP/2.0\r\nHost: x\r\n\r\n");
    CHECK(parser.state() == ParseState::Failed);
    CHECK_EQ(parser.errorStatus(), 505);
}

TEST(RejectsMissingHostOnHttp11) {
    auto parser = parseAll("GET / HTTP/1.1\r\n\r\n");
    CHECK(parser.state() == ParseState::Failed);
    CHECK_EQ(parser.errorStatus(), 400);
}

TEST(RejectsChunkedEncodingWith501) {
    auto parser = parseAll("POST /a HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n");
    CHECK(parser.state() == ParseState::Failed);
    CHECK_EQ(parser.errorStatus(), 501);
}

TEST(RejectsOversizedBodyWith413) {
    ParserLimits limits;
    limits.maxBodyBytes = 16;
    auto parser = parseAll("POST /a HTTP/1.1\r\nHost: x\r\nContent-Length: 64\r\n\r\n", limits);
    CHECK(parser.state() == ParseState::Failed);
    CHECK_EQ(parser.errorStatus(), 413);
}

TEST(RejectsOversizedHeadersWith413) {
    ParserLimits limits;
    limits.maxHeaderBytes = 64;
    RequestParser parser(limits);
    std::string request = "GET / HTTP/1.1\r\nHost: x\r\nX-Big: " + std::string(200, 'a');
    CHECK(parser.consume(request) == ParseState::Failed);
    CHECK_EQ(parser.errorStatus(), 413);
}

TEST(RejectsInvalidContentLength) {
    auto parser = parseAll("POST /a HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n");
    CHECK(parser.state() == ParseState::Failed);
    CHECK_EQ(parser.errorStatus(), 400);
}

TEST(RejectsInvalidPercentEncoding) {
    auto parser = parseAll("GET /%zz HTTP/1.1\r\nHost: x\r\n\r\n");
    CHECK(parser.state() == ParseState::Failed);
    CHECK_EQ(parser.errorStatus(), 400);
}

TEST(DecodesPercentEncodedPath) {
    auto parser = parseAll("GET /a%20b/c HTTP/1.1\r\nHost: x\r\n\r\n");
    REQUIRE(parser.state() == ParseState::Complete);
    CHECK_EQ(parser.request().path, std::string("/a b/c"));
}

TEST(NormalisesDotSegmentsInPath) {
    CHECK_EQ(normalisePath("/a/b/../c"), std::string("/a/c"));
    CHECK_EQ(normalisePath("/../../etc/passwd"), std::string("/etc/passwd"));
    CHECK_EQ(normalisePath("/a/./b//c"), std::string("/a/b/c"));
    CHECK_EQ(normalisePath("/"), std::string("/"));
    CHECK_EQ(normalisePath("/dir/"), std::string("/dir/"));
}

TEST(EncodedTraversalIsNormalisedAway) {
    auto parser = parseAll("GET /static/..%2f..%2fsecret.txt HTTP/1.1\r\nHost: x\r\n\r\n");
    REQUIRE(parser.state() == ParseState::Complete);
    CHECK_EQ(parser.request().path, std::string("/secret.txt"));
}

TEST(PercentDecodeRejectsEmbeddedNul) {
    std::string out;
    CHECK(!percentDecode("/a%00b", out, false));
}

TEST(ConnectionCloseDisablesKeepAlive) {
    auto parser = parseAll("GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n");
    REQUIRE(parser.state() == ParseState::Complete);
    CHECK(!parser.request().keepAlive());
}

TEST(Http10DefaultsToConnectionClose) {
    auto parser = parseAll("GET / HTTP/1.0\r\n\r\n");
    REQUIRE(parser.state() == ParseState::Complete);
    CHECK(!parser.request().keepAlive());
}
