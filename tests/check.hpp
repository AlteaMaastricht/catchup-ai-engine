#pragma once

#include <cstdio>
#include <cstdlib>
#include <string>

inline int g_checks = 0;
inline int g_fails = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        ++g_checks;                                                          \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,     \
                         #cond);                                             \
            ++g_fails;                                                       \
        }                                                                    \
    } while (0)

#define CHECK_EQ(a, b)                                                       \
    do {                                                                     \
        ++g_checks;                                                          \
        auto _va = (a);                                                      \
        auto _vb = (b);                                                      \
        if (!(_va == _vb)) {                                                 \
            std::fprintf(stderr, "FAIL %s:%d: %s == %s  (%s vs %s)\n",       \
                         __FILE__, __LINE__, #a, #b,                         \
                         std::to_string(_va).c_str(),                        \
                         std::to_string(_vb).c_str());                       \
            ++g_fails;                                                       \
        }                                                                    \
    } while (0)

#define CHECK_NE(a, b) CHECK((a) != (b))
#define CHECK_GT(a, b) CHECK((a) > (b))
#define CHECK_GE(a, b) CHECK((a) >= (b))
#define CHECK_LT(a, b) CHECK((a) < (b))
#define CHECK_LE(a, b) CHECK((a) <= (b))

void run_board_tests();
void run_state_tests();
void run_rules_tests();
void run_search_tests();
void run_ui_tests();
void run_reference_tests();
