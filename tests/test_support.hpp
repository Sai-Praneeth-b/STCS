// test_support.hpp -- a tiny test harness (no external framework).
//
// Each test has an ID (a Phase 1 ID such as T-13 where one exists, otherwise
// a Phase 2 ID such as U-12 or X-03), a one-line description and a function.
// CHECK failures throw; the runner prints one PASS/FAIL line per test and the
// program exits non-zero if any test failed. Results printed here are the
// ones recorded in the Phase 2 test report.
#pragma once

#include <exception>
#include <functional>
#include <iostream>
#include <sstream>
#include <string>

namespace testing {

struct Failure : std::exception {
    std::string text;
    explicit Failure(std::string t) : text(std::move(t)) {}
    const char* what() const noexcept override { return text.c_str(); }
};

inline int& failed_count() { static int n = 0; return n; }
inline int& passed_count() { static int n = 0; return n; }

inline void run(const std::string& id, const std::string& description,
                const std::function<void()>& body) {
    try {
        body();
        ++passed_count();
        std::cout << "PASS  " << id << "  " << description << std::endl;
    } catch (const std::exception& problem) {
        ++failed_count();
        std::cout << "FAIL  " << id << "  " << description << "\n        " << problem.what()
                  << std::endl;
    }
}

inline int summarize(const char* suite) {
    std::cout << suite << ": " << passed_count() << " passed, " << failed_count() << " failed"
              << std::endl;
    return failed_count() == 0 ? 0 : 1;
}

}  // namespace testing

#define CHECK(condition)                                                              \
    do {                                                                              \
        if (!(condition)) {                                                           \
            std::ostringstream message_;                                              \
            message_ << "CHECK failed: " #condition " (line " << __LINE__ << ")";     \
            throw testing::Failure(message_.str());                                   \
        }                                                                             \
    } while (0)

#define CHECK_EQ(actual, expected)                                                    \
    do {                                                                              \
        const auto& a_ = (actual);                                                    \
        const auto& e_ = (expected);                                                  \
        if (!(a_ == e_)) {                                                            \
            std::ostringstream message_;                                              \
            message_ << "CHECK_EQ failed at line " << __LINE__ << ": " #actual        \
                     << " was [" << a_ << "], expected [" << e_ << "]";               \
            throw testing::Failure(message_.str());                                   \
        }                                                                             \
    } while (0)
