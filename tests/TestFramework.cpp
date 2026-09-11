#include "TestFramework.hpp"

#include <exception>
#include <iostream>

namespace testing {

int g_currentFailures = 0;

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

Registrar::Registrar(const char* name, std::function<void()> body) {
    registry().push_back(TestCase{name, std::move(body)});
}

void reportFailure(const char* file, int line, const std::string& message) {
    ++g_currentFailures;
    std::cout << "    FAIL " << file << ':' << line << ": " << message << '\n';
}

}  // namespace testing

int main(int argc, char** argv) {
    const std::string filter = argc > 1 ? argv[1] : std::string();

    int failed = 0;
    int run = 0;
    for (const auto& test : testing::registry()) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) {
            continue;
        }
        ++run;
        testing::g_currentFailures = 0;
        std::cout << "[ RUN  ] " << test.name << '\n';
        try {
            test.body();
        } catch (const std::exception& error) {
            testing::reportFailure(__FILE__, __LINE__,
                                   std::string("unexpected exception: ") + error.what());
        } catch (...) {
            testing::reportFailure(__FILE__, __LINE__, "unexpected non-standard exception");
        }
        if (testing::g_currentFailures > 0) {
            ++failed;
            std::cout << "[ FAIL ] " << test.name << '\n';
        } else {
            std::cout << "[  OK  ] " << test.name << '\n';
        }
    }

    std::cout << "\n" << (run - failed) << '/' << run << " tests passed\n";
    return failed == 0 ? 0 : 1;
}
