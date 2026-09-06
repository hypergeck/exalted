#pragma once
// Tiny assertion harness: no dependencies, one executable per test file,
// process exit code = number of failures.
#include <cstdio>
#include <string>

inline int g_failures = 0;
inline int g_checks = 0;

#define CHECK(cond)                                                                        \
    do {                                                                                   \
        ++g_checks;                                                                        \
        if (!(cond)) { ++g_failures; std::fprintf(stderr, "  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

#define CHECK_EQ(a, b)                                                                     \
    do {                                                                                   \
        ++g_checks;                                                                        \
        const auto _va = (a); const auto _vb = (b);                                        \
        if (!(_va == _vb)) {                                                               \
            ++g_failures;                                                                  \
            std::fprintf(stderr, "  FAIL %s:%d  %s == %s  (got %s vs %s)\n", __FILE__, __LINE__, #a, #b, \
                         std::to_string(_va).c_str(), std::to_string(_vb).c_str());        \
        }                                                                                  \
    } while (0)

#define TEST_MAIN(name)                                                                    \
    int main() {                                                                           \
        run_tests();                                                                       \
        std::printf("%-28s %3d checks, %d failures\n", name, g_checks, g_failures);        \
        return g_failures;                                                                 \
    }
