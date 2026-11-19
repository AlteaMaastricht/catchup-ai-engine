#include "check.hpp"
#include "position.hpp"

#include <algorithm>
#include <cstring>
#include <random>
#include <utility>
#include <vector>

using namespace catchup;

namespace {

void assert_consistent(const GameState& state) {
    for (int player : {WHITE, BLACK}) {
        Recompute expected = recompute(state, player);
        CHECK_EQ(state.largest[player], expected.largest);
        CHECK_EQ(state.sumsq[player], expected.sumsq);
        CHECK_EQ(state.nstones[player], expected.nstones);
        CHECK_EQ(state.centrality[player], expected.centrality);
        CHECK_EQ(state.frontier[player], expected.frontier);
        CHECK(state.group_sizes(player) == expected.groups);
        CHECK_EQ(state.second[player], expected.second);
        CHECK_EQ(state.largest_liberties(player), expected.liberties);
        for (int c = 0; c < NUM_CELLS; ++c) {
            CHECK_EQ(static_cast<int>(state.adjacency[player][c]), expected.adjacency[c]);
        }
        std::uint64_t touch = 0, join = 0;
        for (int cell = 0; cell < NUM_CELLS; ++cell) {
            int a = state.adjacency[player][cell];
            if (a) {
                touch |= std::uint64_t{1} << cell;
                if (a >= 2) join |= std::uint64_t{1} << cell;
            }
        }
        CHECK_EQ(state.touch[player], touch);
        CHECK_EQ(state.join[player], join);

        int lg = state.largest[player];
        int root = state.largest_root[player];
        bool unique = state.nstones[player] > 0 && lg >= 1 && lg <= NUM_CELLS
                      && state.size_n[player][lg] == 1;
        if (!unique) {
            CHECK_EQ(root, -1);
        } else {
            bool ok = root >= 0 && root < NUM_CELLS
                      && state.parent[root] == root
                      && static_cast<int>(state.colour[root]) == STONE[player]
                      && state.size[root] == lg;
            CHECK(ok);
        }
    }
    CHECK_EQ(state.zobrist, recompute_zobrist(state));
    CHECK_EQ(state.empty_count,
             NUM_CELLS - state.nstones[WHITE] - state.nstones[BLACK]);
    for (int player : {WHITE, BLACK}) {
        std::uint64_t expected_bb = 0;
        int stone = STONE[player];
        for (int cell = 0; cell < NUM_CELLS; ++cell) {
            if (state.colour[cell] == stone) expected_bb |= std::uint64_t{1} << cell;
        }
        CHECK_EQ(state.bb[player], expected_bb);
    }
}

int lex_from_sizes(const std::vector<int>& white, const std::vector<int>& black) {
    int n = static_cast<int>(std::max(white.size(), black.size()));
    for (int i = 0; i < n; ++i) {
        int w = i < static_cast<int>(white.size()) ? white[i] : 0;
        int b = i < static_cast<int>(black.size()) ? black[i] : 0;
        if (w != b) return w > b ? 1 : -1;
    }
    return 0;
}

}  // namespace

void run_state_tests() {
    for (std::uint32_t seed = 0; seed < 6; ++seed) {
        auto pair = random_playout(seed);
        for (int player : {WHITE, BLACK}) {
            CHECK(pair.first.group_sizes(player) ==
                  groups_by_flood_fill(pair.first, player));
        }
    }

    GameState pos = random_position(3, 40);
    pos.sanity_check();
    for (int cell = 0; cell < NUM_CELLS; ++cell) {
        if (pos.colour[cell]) {
            int root = pos.find(cell);
            CHECK_EQ(pos.parent[root], root);
            CHECK_EQ(static_cast<int>(pos.colour[root]), static_cast<int>(pos.colour[cell]));
        }
    }

    pos = random_position(4, 45);
    for (int player : {WHITE, BLACK}) {
        int sum = 0;
        for (int s : pos.group_sizes(player)) sum += s;
        CHECK_EQ(sum, pos.nstones[player]);
    }

    {
        GameState state;
        int centre = name_to_index("E5");
        std::vector<int> chosen;
        for (int i = 0; i < NEIGHBOUR_COUNT[centre]; ++i) {
            int candidate = NEIGHBOURS[centre][i];
            bool ok = true;
            for (int c : chosen) {
                for (int j = 0; j < NEIGHBOUR_COUNT[c]; ++j) {
                    if (NEIGHBOURS[c][j] == candidate) ok = false;
                }
            }
            if (ok) chosen.push_back(candidate);
        }
        CHECK_EQ(static_cast<int>(chosen.size()), 3);
        state.to_move = WHITE;
        for (int cell : chosen) state.place(cell);
        auto sizes = state.group_sizes(WHITE);
        std::sort(sizes.begin(), sizes.end());
        CHECK(sizes == std::vector<int>({1, 1, 1}));
        state.place(centre);
        CHECK(state.group_sizes(WHITE) == std::vector<int>({4}));
        CHECK_EQ(state.largest[WHITE], 4);
    }

    for (auto spec : {std::pair<int, int>{0, 20}, {1, 40}, {7, 55}}) {
        GameState state = random_position(spec.first, spec.second);
        CHECK_EQ(state.compare_final(),
                 lex_from_sizes(state.group_sizes(WHITE), state.group_sizes(BLACK)));
    }
    {
        auto pair = random_playout(3);
        CHECK_EQ(pair.first.compare_final(),
                 lex_from_sizes(pair.first.group_sizes(WHITE),
                                pair.first.group_sizes(BLACK)));
    }

    {
        GameState state;
        state.to_move = WHITE;
        for (const char* n : {"E5", "E4", "A1"}) state.place(name_to_index(n));
        state.to_move = BLACK;
        state.zobrist ^= ZOBRIST_SIDE;
        for (const char* n : {"I9", "I8"}) state.place(name_to_index(n));
        CHECK_EQ(state.compare_final(), 1);
    }

    {
        GameState state;
        std::mt19937 rng(777);
        while (!state.is_over()) {
            std::uniform_int_distribution<int> dist(1, state.max_this_turn);
            int count = dist(rng);
            std::vector<int> empty;
            for (int c = 0; c < NUM_CELLS; ++c)
                if (!state.colour[c]) empty.push_back(c);
            std::shuffle(empty.begin(), empty.end(), rng);
            for (int i = 0; i < count; ++i) {
                state.place(empty[i]);
                assert_consistent(state);
            }
            state.end_turn();
            assert_consistent(state);
        }
    }

    for (std::uint32_t seed = 0; seed < 4; ++seed) {
            auto pair = random_playout(seed);
            GameState state = std::move(pair.first);
            for (auto it = pair.second.rbegin(); it != pair.second.rend(); ++it) {
                state.undo_turn(*it);
                assert_consistent(state);
            }
        GameState fresh;
        CHECK(std::memcmp(state.colour, fresh.colour, sizeof(state.colour)) == 0);
        CHECK_EQ(state.bb[0], fresh.bb[0]);
        CHECK_EQ(state.bb[1], fresh.bb[1]);
        CHECK_EQ(state.empty_count, fresh.empty_count);
        CHECK_EQ(state.to_move, fresh.to_move);
        CHECK_EQ(state.zobrist, fresh.zobrist);
        CHECK_EQ(state._undo_n, 0);
        CHECK_EQ(state._turn_undo_n, 0);
        CHECK_EQ(state._lib_undo_n, 0);
        CHECK_EQ(state.allows_three, fresh.allows_three);
    }

    {
        GameState state = random_position(9, 30);
        auto snapshot_colour = std::vector<std::uint8_t>(state.colour, state.colour + NUM_CELLS);
        auto snapshot_parent = std::vector<int>(state.parent, state.parent + NUM_CELLS);
        std::uint64_t z = state.zobrist;
        int empty = state.empty_count;
        for (int cell = 0; cell < NUM_CELLS; ++cell) {
            if (state.colour[cell]) continue;
            state.place(cell);
            state.unplace();
        }
        CHECK(std::vector<std::uint8_t>(state.colour, state.colour + NUM_CELLS) == snapshot_colour);
        CHECK(std::vector<int>(state.parent, state.parent + NUM_CELLS) == snapshot_parent);
        CHECK_EQ(state.zobrist, z);
        CHECK_EQ(state.empty_count, empty);
    }

    for (std::uint32_t seed = 0; seed < 4; ++seed) {
        auto pair = random_playout(seed);
        CHECK_EQ(pair.first.zobrist, recompute_zobrist(pair.first));
    }

    {
        GameState first;
        first.play_turn({name_to_index("E5")});
        first.play_turn({name_to_index("A1"), name_to_index("I9")});
        GameState second;
        second.play_turn({name_to_index("E5")});
        second.play_turn({name_to_index("I9"), name_to_index("A1")});
        CHECK_EQ(first.zobrist, second.zobrist);
        CHECK_EQ(first.allows_three, second.allows_three);
    }

    {
        GameState state = random_position(13, 25);
        GameState copy = state.clone();
        CHECK_EQ(copy.zobrist, state.zobrist);
        int cell = 0;
        while (copy.colour[cell]) ++cell;
        copy.place(cell);
        CHECK_NE(copy.zobrist, state.zobrist);
        CHECK(!state.colour[cell]);
        CHECK_EQ(state.zobrist, recompute_zobrist(state));
    }

    {
        GameState state = random_position(21, 18);
        int mx0 = state.max_reachable(WHITE);
        int mx1 = state.max_reachable(BLACK);
        int lr0 = state.largest_reachable(WHITE);
        int lr1 = state.largest_reachable(BLACK);
        CHECK_EQ(state.max_reachable(WHITE), mx0);
        CHECK_EQ(state.largest_reachable(BLACK), lr1);
        int cell = 0;
        while (cell < NUM_CELLS && state.colour[cell]) ++cell;
        CHECK_LT(cell, NUM_CELLS);
        state.place(cell);
        int after = state.max_reachable(WHITE);
        CHECK_GE(after, 0);
        state.unplace();
        CHECK_EQ(state.max_reachable(WHITE), mx0);
        CHECK_EQ(state.max_reachable(BLACK), mx1);
        CHECK_EQ(state.largest_reachable(WHITE), lr0);
        CHECK_EQ(state.largest_reachable(BLACK), lr1);
        CHECK(state.cache_max_valid[WHITE]);
        CHECK(state.cache_lr_valid[BLACK]);
    }
}
