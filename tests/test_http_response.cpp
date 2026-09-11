#include "TestFramework.hpp"
#include "http/HttpResponse.hpp"

using namespace hs;

TEST(SerialisesStatusLineAndHeaders) {
    HttpResponse response = HttpResponse::text(200, "hello");
    response.setHeader("X-Test", "value");
    const std::string raw = response.serialize();

    CHECK(raw.rfind("HTTP/1.1 200 OK\r\n", 0) == 0);
    CHECK(raw.find("Content-Type: text/plain; charset=utf-8\r\n") != std::string::npos);
    CHECK(raw.find("X-Test: value\r\n") != std::string::npos);
    CHECK(raw.find("Content-Length: 5\r\n") != std::string::npos);
    CHECK(raw.find("\r\n\r\nhello") != std::string::npos);
}

TEST(SetHeaderReplacesExistingValue) {
    HttpResponse response = HttpResponse::text(200, "x");
    response.setHeader("Connection", "keep-alive");
    response.setHeader("connection", "close");

    const std::string raw = response.serialize();
    CHECK(raw.find("close") != std::string::npos);
    CHECK(raw.find("keep-alive") == std::string::npos);
}

TEST(ContentLengthAlwaysMatchesBody) {
    HttpResponse response = HttpResponse::text(200, "1234567890");
    response.setHeader("Content-Length", "999");  // must be ignored
    CHECK(response.serialize().find("Content-Length: 10\r\n") != std::string::npos);
}

TEST(HeadResponseKeepsLengthButOmitsBody) {
    HttpResponse response = HttpResponse::text(200, "body-text");
    const std::string raw = response.serialize(false);
    CHECK(raw.find("Content-Length: 9\r\n") != std::string::npos);
    CHECK(raw.find("body-text") == std::string::npos);
    CHECK(raw.substr(raw.size() - 4) == "\r\n\r\n");
}

TEST(NoContentResponseHasNoBodyOrLength) {
    const std::string raw = HttpResponse::noContent().serialize();
    CHECK(raw.rfind("HTTP/1.1 204 No Content\r\n", 0) == 0);
    CHECK(raw.find("Content-Length") == std::string::npos);
}

TEST(ErrorResponseEscapesDetailText) {
    const std::string raw = HttpResponse::error(404, "<script>alert(1)</script>").serialize();
    CHECK(raw.find("<script>") == std::string::npos);
    CHECK(raw.find("&lt;script&gt;") != std::string::npos);
}

TEST(KnownStatusCodesHaveReasonPhrases) {
    CHECK_EQ(std::string(reasonPhrase(201)), std::string("Created"));
    CHECK_EQ(std::string(reasonPhrase(405)), std::string("Method Not Allowed"));
    CHECK_EQ(std::string(reasonPhrase(408)), std::string("Request Timeout"));
    CHECK_EQ(std::string(reasonPhrase(413)), std::string("Payload Too Large"));
    CHECK_EQ(std::string(reasonPhrase(503)), std::string("Service Unavailable"));
}
