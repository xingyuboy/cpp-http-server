#pragma once

#include <functional>
#include <sstream>
#include <string>
#include <vector>

// A dependency-free test harness. Pulling in a framework would mean either
// vendoring it or requiring a network fetch at configure time, and the project
// only needs test registration, assertions and a summary.
namespace testing {

struct TestCase {
    std::string name;
    std::function<void()> body;
};

std::vector<TestCase>& registry();
extern int g_currentFailures;
void reportFailure(const char* file, int line, const std::string& message);

struct Registrar {
    Registrar(const char* name, std::function<void()> body);
};

template <typename A, typename B>
void checkEqual(const char* file, int line, const char* expression, const A& actual,
                const B& expected) {
    if (!(actual == expected)) {
        std::ostringstream message;
        message << expression << "\n      actual:   " << actual << "\n      expected: " << expected;
        reportFailure(file, line, message.str());
    }
}

}  // namespace testing

#define TEST(testName)                                                     \
    static void testName();                                                \
    static ::testing::Registrar registrar_##testName(#testName, testName); \
    static void testName()

#define CHECK(condition)                                                      \
    do {                                                                      \
        if (!(condition)) {                                                    \
            ::testing::reportFailure(__FILE__, __LINE__, "CHECK(" #condition ")"); \
        }                                                                     \
    } while (false)

#define CHECK_EQ(actual, expected) \
    ::testing::checkEqual(__FILE__, __LINE__, #actual " == " #expected, actual, expected)

// Use when continuing after the failure would crash or cascade.
#define REQUIRE(condition)                                                        \
    do {                                                                          \
        if (!(condition)) {                                                        \
            ::testing::reportFailure(__FILE__, __LINE__, "REQUIRE(" #condition ")"); \
            return;                                                               \
        }                                                                         \
    } while (false)
