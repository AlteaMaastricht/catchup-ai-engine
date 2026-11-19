#include "check.hpp"

#include <cstdio>

int main() {
    run_board_tests();
    run_state_tests();
    run_rules_tests();
    run_ui_tests();
    run_search_tests();
    run_reference_tests();
    if (g_fails) {
        std::fprintf(stderr, "\n%d checks, %d FAILED\n", g_checks, g_fails);
        return 1;
    }
    std::printf("%d checks passed\n", g_checks);
    return 0;
}
