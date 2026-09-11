#include <filesystem>
#include <fstream>
#include <random>

#include "TestFramework.hpp"
#include "http/StaticFiles.hpp"
#include "http/Url.hpp"

using namespace hs;

namespace {

// Builds a throwaway document root with a secret file placed *outside* it, so
// traversal attempts have something real to try to reach.
class TempTree {
public:
    TempTree() {
        base_ = std::filesystem::temp_directory_path() /
                ("hs-static-test-" + std::to_string(std::random_device{}()));
        root_ = base_ / "public";
        std::filesystem::create_directories(root_ / "assets");
        write(root_ / "index.html", "<h1>home</h1>");
        write(root_ / "about.html", "<h1>about</h1>");
        write(root_ / "assets" / "style.css", "body{}");
        write(base_ / "secret.txt", "top secret");
    }

    ~TempTree() {
        std::error_code error;
        std::filesystem::remove_all(base_, error);
    }

    const std::filesystem::path& root() const { return root_; }

private:
    static void write(const std::filesystem::path& path, std::string_view contents) {
        std::ofstream file(path, std::ios::binary);
        file << contents;
    }

    std::filesystem::path base_;
    std::filesystem::path root_;
};

HttpRequest get(const std::string& path) {
    HttpRequest request;
    request.method = Method::Get;
    request.methodText = "GET";
    request.path = normalisePath(path);
    request.target = path;
    return request;
}

}  // namespace

TEST(ServesExistingFile) {
    TempTree tree;
    StaticFileHandler handler(tree.root());
    HttpRequest request = get("/index.html");
    const HttpResponse response = handler.handle(request);

    CHECK_EQ(response.status(), 200);
    CHECK_EQ(response.body(), std::string("<h1>home</h1>"));
    REQUIRE(response.header("Content-Type") != nullptr);
    CHECK_EQ(*response.header("Content-Type"), std::string("text/html; charset=utf-8"));
}

TEST(ServesIndexForDirectory) {
    TempTree tree;
    StaticFileHandler handler(tree.root());
    HttpRequest request = get("/");
    CHECK_EQ(handler.handle(request).body(), std::string("<h1>home</h1>"));
}

TEST(ServesExtensionlessPathAsHtml) {
    TempTree tree;
    StaticFileHandler handler(tree.root());
    HttpRequest request = get("/about");
    const HttpResponse response = handler.handle(request);
    CHECK_EQ(response.status(), 200);
    CHECK_EQ(response.body(), std::string("<h1>about</h1>"));
}

TEST(ServesNestedFileWithCorrectMimeType) {
    TempTree tree;
    StaticFileHandler handler(tree.root());
    HttpRequest request = get("/assets/style.css");
    const HttpResponse response = handler.handle(request);
    CHECK_EQ(response.status(), 200);
    CHECK_EQ(*response.header("Content-Type"), std::string("text/css; charset=utf-8"));
}

TEST(MissingFileReturns404) {
    TempTree tree;
    StaticFileHandler handler(tree.root());
    HttpRequest request = get("/nope.html");
    CHECK_EQ(handler.handle(request).status(), 404);
}

TEST(TraversalWithDotSegmentsCannotEscapeRoot) {
    TempTree tree;
    StaticFileHandler handler(tree.root());
    for (const char* path : {"/../secret.txt", "/assets/../../secret.txt",
                             "/./../../secret.txt", "/a/b/../../../secret.txt"}) {
        HttpRequest request = get(path);
        const HttpResponse response = handler.handle(request);
        CHECK(response.status() == 404 || response.status() == 403);
        CHECK(response.body().find("top secret") == std::string::npos);
    }
}

TEST(RawTraversalWithoutNormalisationIsRefused) {
    // Bypasses the parser to prove the handler defends itself independently.
    TempTree tree;
    StaticFileHandler handler(tree.root());
    HttpRequest request;
    request.method = Method::Get;
    request.methodText = "GET";
    request.path = "/../secret.txt";
    const HttpResponse response = handler.handle(request);
    CHECK_EQ(response.status(), 403);
    CHECK(response.body().find("top secret") == std::string::npos);
}

TEST(UnsupportedMethodReturns405) {
    TempTree tree;
    StaticFileHandler handler(tree.root());
    HttpRequest request = get("/index.html");
    request.method = Method::Post;
    request.methodText = "POST";
    const HttpResponse response = handler.handle(request);
    CHECK_EQ(response.status(), 405);
    REQUIRE(response.header("Allow") != nullptr);
}

TEST(MissingDocumentRootIsRejectedAtConstruction) {
    bool threw = false;
    try {
        StaticFileHandler handler("./no-such-document-root");
    } catch (const std::exception&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(MimeTypeLookupCoversCommonExtensions) {
    CHECK_EQ(std::string(mimeTypeForExtension(".js")),
             std::string("application/javascript; charset=utf-8"));
    CHECK_EQ(std::string(mimeTypeForExtension(".PNG")), std::string("image/png"));
    CHECK_EQ(std::string(mimeTypeForExtension(".unknown")),
             std::string("application/octet-stream"));
}
