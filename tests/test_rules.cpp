#include "check.hpp"
#include "position.hpp"
#include "ui.hpp"

#include <cstring>
#include <initializer_list>
#include <vector>

using namespace catchup;

namespace {

GameState fixed_groups(const std::vector<int>& white, const std::vector<int>& black) {
    GameState s;
    for (int v : white) s.size_n[WHITE][v] += 1;
    for (int v : black) s.size_n[BLACK][v] += 1;
    return s;
}

std::vector<int> cells_of(std::initializer_list<const char*> names) {
    std::vector<int> out;
    for (auto n : names) out.push_back(name_to_index(n));
    return out;
}

}  // namespace

void run_rules_tests() {
    {
        GameState state;
        CHECK_EQ(state.max_this_turn, 1);
        CHECK(state.legal_turn_sizes() == std::vector<int>({1}));
        bool threw = false;
        try { state.play_turn(cells_of({"E5", "D4"})); } catch (const IllegalMove&) { threw = true; }
        CHECK(threw);
        CHECK_EQ(state.score(WHITE), 1);
        CHECK_EQ(state.score(BLACK), 1);
    }

    for (const char* opening : {"E5", "A1", "I9"}) {
        GameState state;
        state.play_turn(cells_of({opening}));
        CHECK_EQ(state.score(WHITE), 1);
        CHECK(!state.score_increased[WHITE]);
        CHECK_EQ(state.max_this_turn, 2);
    }

    {
        GameState state;
        bool threw = false;
        try { state.play_turn({}); } catch (const IllegalMove&) { threw = true; }
        CHECK(threw);
    }

    {
        GameState state;
        while (state.empty_count > 1) {
            int cell = 0;
            while (state.colour[cell]) ++cell;
            state.play_turn({cell});
        }
        CHECK_EQ(state.empty_count, 1);
        CHECK_EQ(state.max_this_turn, 1);
        bool threw = false;
        try { state.play_turn(cells_of({"A1", "B1"})); } catch (const IllegalMove&) { threw = true; }
        CHECK(threw);
    }

    {
        GameState state;
        state.play_turn(cells_of({"E5"}));
        state.play_turn(cells_of({"A1", "B1"}));
        CHECK(state.score_increased[BLACK]);
        CHECK_EQ(state.score(BLACK), 2);
        CHECK(state.score(BLACK) >= state.score(WHITE));
        CHECK_EQ(state.max_this_turn, 3);
        CHECK(state.allows_three);
    }

    {
        GameState state;
        state.play_turn(cells_of({"E5"}));
        state.play_turn(cells_of({"A1", "I9"}));
        CHECK(!state.score_increased[BLACK]);
        CHECK_EQ(state.score(BLACK), 1);
        CHECK_EQ(state.max_this_turn, 2);
    }

    {
        GameState state;
        state.play_turn(cells_of({"E5"}));
        state.play_turn(cells_of({"A1"}));
        state.play_turn(cells_of({"E4", "F5"}));
        state.play_turn(cells_of({"I9"}));
        state.play_turn(cells_of({"F6", "D4"}));
        CHECK_EQ(state.score(WHITE), 5);
        state.play_turn(cells_of({"A2", "B2"}));
        CHECK(state.score_increased[BLACK]);
        CHECK_EQ(state.score(BLACK), 3);
        CHECK(state.score(BLACK) < state.score(WHITE));
        CHECK_EQ(state.max_this_turn, 2);
    }

    {
        GameState state;
        state.play_turn(cells_of({"E5"}));
        state.play_turn(cells_of({"A1", "B1"}));
        state.play_turn(cells_of({"E4", "F5"}));
        CHECK(state.score_increased[WHITE]);
        CHECK_EQ(state.max_this_turn, 3);
    }

    {
        GameState denying;
        denying.play_turn(cells_of({"E5"}));
        denying.play_turn(cells_of({"A1", "B1"}));
        denying.play_turn(cells_of({"F5"}));
        denying.play_turn(cells_of({"I9"}));

        GameState allowing;
        allowing.play_turn(cells_of({"E5"}));
        allowing.play_turn(cells_of({"A1", "I9"}));
        allowing.play_turn(cells_of({"F5"}));
        allowing.play_turn(cells_of({"B1"}));

        CHECK(std::memcmp(denying.colour, allowing.colour, NUM_CELLS) == 0);
        CHECK_EQ(denying.to_move, allowing.to_move);
        CHECK_EQ(denying.score(WHITE), allowing.score(WHITE));
        CHECK_EQ(denying.score(BLACK), allowing.score(BLACK));
        CHECK(!denying.allows_three);
        CHECK_EQ(denying.max_this_turn, 2);
        CHECK(allowing.allows_three);
        CHECK_EQ(allowing.max_this_turn, 3);
        CHECK_NE(denying.zobrist, allowing.zobrist);
    }

    CHECK_EQ(fixed_groups({10}, {9}).compare_final(), 1);
    CHECK_EQ(fixed_groups({9}, {10}).compare_final(), -1);
    CHECK_EQ(fixed_groups({6, 5, 5, 3}, {6, 5, 4, 3}).compare_final(), 1);
    CHECK_EQ(fixed_groups({6, 5, 4, 3}, {6, 5, 5, 3}).compare_final(), -1);
    CHECK_EQ(fixed_groups({6, 5, 4, 3}, {6, 5, 4, 3}).compare_final(), 0);
    CHECK_EQ(fixed_groups({6, 5}, {6, 5, 1}).compare_final(), -1);
    CHECK_EQ(fixed_groups({6, 5, 1}, {6, 5}).compare_final(), 1);
    CHECK_EQ(fixed_groups({7, 2}, {7, 1, 1, 1}).compare_final(), 1);

    {
        GameState won = fixed_groups({8}, {7});
        CHECK_EQ(won.terminal_score(WHITE), WIN_SCORE);
        CHECK_EQ(won.terminal_score(BLACK), -WIN_SCORE);
        GameState drawn = fixed_groups({8}, {8});
        CHECK_EQ(drawn.terminal_score(WHITE), 0);
        CHECK_EQ(drawn.terminal_score(BLACK), 0);
    }

    {
        GameState state = random_position(11, -1, false);
        CHECK(state.is_over());
        CHECK_EQ(state.empty_count, 0);
        CHECK_EQ(state.nstones[WHITE] + state.nstones[BLACK], 61);
    }
    {
        GameState state = random_position(12, -1, false);
        int total = 0;
        for (int s : state.group_sizes(WHITE)) total += s;
        for (int s : state.group_sizes(BLACK)) total += s;
        CHECK_EQ(total, 61);
    }

    {
        GameState state;
        state.play_turn(cells_of({"E5"}));
        bool threw = false;
        try { state.play_turn(cells_of({"E5"})); } catch (const IllegalMove&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { state.play_turn({name_to_index("A1"), name_to_index("A1")}); }
        catch (const IllegalMove&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { state.play_turn({61}); } catch (const IllegalMove&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { state.play_turn({-1}); } catch (const IllegalMove&) { threw = true; }
        CHECK(threw);
    }
}
