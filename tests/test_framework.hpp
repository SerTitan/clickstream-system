#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <sstream>

namespace test {

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

inline std::vector<TestCase>& get_tests() {
    static std::vector<TestCase> tests;
    return tests;
}

inline int& get_failures() {
    static int failures = 0;
    return failures;
}

inline std::string& current_test() {
    static std::string name;
    return name;
}

struct TestRegistrar {
    TestRegistrar(const char* name, std::function<void()> fn) {
        get_tests().push_back({name, fn});
    }
};

#define TEST(name) \
    void test_##name(); \
    static test::TestRegistrar registrar_##name(#name, test_##name); \
    void test_##name()

#define ASSERT_TRUE(cond) \
    do { \
        if (!(cond)) { \
            std::cerr << "  FAIL: " << test::current_test() << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::cerr << "    Expected: " << #cond << " to be true\n"; \
            test::get_failures()++; \
            return; \
        } \
    } while(0)

#define ASSERT(cond) ASSERT_TRUE(cond)

#define ASSERT_FALSE(cond) \
    do { \
        if (cond) { \
            std::cerr << "  FAIL: " << test::current_test() << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::cerr << "    Expected: " << #cond << " to be false\n"; \
            test::get_failures()++; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b) \
    do { \
        auto _a = (a); \
        auto _b = (b); \
        if (_a != _b) { \
            std::cerr << "  FAIL: " << test::current_test() << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::cerr << "    Expected: " << #a << " == " << #b << "\n"; \
            std::cerr << "    Got: " << _a << " != " << _b << "\n"; \
            test::get_failures()++; \
            return; \
        } \
    } while(0)

#define ASSERT_NE(a, b) \
    do { \
        auto _a = (a); \
        auto _b = (b); \
        if (_a == _b) { \
            std::cerr << "  FAIL: " << test::current_test() << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::cerr << "    Expected: " << #a << " != " << #b << "\n"; \
            std::cerr << "    Got: " << _a << " == " << _b << "\n"; \
            test::get_failures()++; \
            return; \
        } \
    } while(0)

#define ASSERT_CONTAINS(str, substr) \
    do { \
        std::string _str = (str); \
        std::string _substr = (substr); \
        if (_str.find(_substr) == std::string::npos) { \
            std::cerr << "  FAIL: " << test::current_test() << " at " << __FILE__ << ":" << __LINE__ << "\n"; \
            std::cerr << "    Expected: \"" << _str << "\" to contain \"" << _substr << "\"\n"; \
            test::get_failures()++; \
            return; \
        } \
    } while(0)

inline int run_all_tests() {
    int passed = 0;
    int failed = 0;

    std::cout << "Running " << get_tests().size() << " tests...\n";
    std::cout << "========================================\n";

    for (auto& tc : get_tests()) {
        current_test() = tc.name;
        int before = get_failures();

        try {
            tc.fn();
        } catch (const std::exception& e) {
            std::cerr << "  FAIL: " << tc.name << " threw exception: " << e.what() << "\n";
            get_failures()++;
        } catch (...) {
            std::cerr << "  FAIL: " << tc.name << " threw unknown exception\n";
            get_failures()++;
        }

        if (get_failures() == before) {
            std::cout << "  PASS: " << tc.name << "\n";
            passed++;
        } else {
            failed++;
        }
    }

    std::cout << "========================================\n";
    std::cout << "Results: " << passed << " passed, " << failed << " failed\n";

    return failed > 0 ? 1 : 0;
}

} // namespace test
