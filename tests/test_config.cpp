#include "TestFramework.hpp"
#include "config/Config.hpp"

using namespace hs;

TEST(ParsesTypicalConfigFile) {
    const Config config = Config::parse(
        "# a comment\n"
        "port = 9090\n"
        "document_root = ./www\n"
        "worker_threads = 8\n"
        "request_timeout = 2500\n"
        "\n"
        "log_level = debug\n");

    CHECK_EQ(static_cast<int>(config.port), 9090);
    CHECK_EQ(config.documentRoot, std::string("./www"));
    CHECK_EQ(static_cast<int>(config.workerThreads), 8);
    CHECK_EQ(config.requestTimeoutMs, 2500);
    CHECK(config.logLevel == LogLevel::Debug);
}

TEST(UnsetKeysKeepTheirDefaults) {
    const Config config = Config::parse("port = 1234\n");
    CHECK_EQ(config.documentRoot, std::string("./public"));
    CHECK_EQ(config.bindAddress, std::string("127.0.0.1"));
}

TEST(StripsTrailingComments) {
    const Config config = Config::parse("port = 8085   # the listen port\nworker_threads = 2 #two\n");
    CHECK_EQ(static_cast<int>(config.port), 8085);
    CHECK_EQ(static_cast<int>(config.workerThreads), 2);
}

TEST(IgnoresBlankLinesAndSurroundingSpace) {
    const Config config = Config::parse("\n   \n   port   =   8081   \n\n");
    CHECK_EQ(static_cast<int>(config.port), 8081);
}

TEST(ZeroWorkerThreadsResolvesToHardwareConcurrency) {
    const Config config = Config::parse("worker_threads = 0\n");
    CHECK(config.resolvedWorkerThreads() >= 2);
}

TEST(RejectsUnknownSetting) {
    bool threw = false;
    try {
        Config::parse("nonsense = 1\n");
    } catch (const ConfigError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(RejectsNonNumericPort) {
    bool threw = false;
    try {
        Config::parse("port = eighty\n");
    } catch (const ConfigError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(RejectsOutOfRangePort) {
    bool threw = false;
    try {
        Config::parse("port = 70000\n");
    } catch (const ConfigError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(RejectsLineWithoutEquals) {
    bool threw = false;
    try {
        Config::parse("port 8080\n");
    } catch (const ConfigError&) {
        threw = true;
    }
    CHECK(threw);
}

TEST(RejectsMissingConfigFile) {
    bool threw = false;
    try {
        Config::loadFromFile("./definitely-not-here.conf");
    } catch (const ConfigError&) {
        threw = true;
    }
    CHECK(threw);
}
