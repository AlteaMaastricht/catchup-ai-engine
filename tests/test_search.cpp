#include "check.hpp"
#include "position.hpp"
#include "search.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <initializer_list>
#include <set>
#include <utility>
#include <vector>

using namespace catchup;
using Steady = std::chrono::steady_clock;

namespace {

Engine plain_engine(bool use_tt, int max_depth) {
    return Engine(use_tt, false, false, false, false, false, false, max_depth);
}

void play_names(GameState& state, std::initializer_list<const char*> names) {
    std::vector<int> cells;
    for (auto n : names) cells.push_back(name_to_index(n));
    state.play_turn(cells);
}

void play_to_black_gift3_after_white_five(GameState& state) {
    play_names(state, {"E5"});
    play_names(state, {"D4", "F4"});
    play_names(state, {"G5", "C4"});
    play_names(state, {"C2", "B3"});
    play_names(state, {"A3", "D3"});
    play_names(state, {"D6", "G6"});
    play_names(state, {"B5", "G7"});
    play_names(state, {"H5", "G8"});
    play_names(state, {"I7", "E7"});
    play_names(state, {"E2", "E8"});
    play_names(state, {"F5", "D5"});
}

void play_to_h7_i8_decision(GameState& state) {
    play_names(state, {"E5"});
    play_names(state, {"D5", "E4"});
    play_names(state, {"D6", "F7"});
    play_names(state, {"F6", "C6"});
    play_names(state, {"G6", "H8"});
    play_names(state, {"E7", "D8"});
    play_names(state, {"G7", "F8"});
    play_names(state, {"D4", "D7", "B4"});
    play_names(state, {"C5", "E6"});
    play_names(state, {"B5", "C3", "F5"});
    play_names(state, {"B3", "E9"});
    play_names(state, {"C4", "G5"});
    play_names(state, {"H6", "I6", "E8"});
    play_names(state, {"H4", "F3"});
    play_names(state, {"F4", "G4"});
    play_names(state, {"G3", "H5"});
    play_names(state, {"I5", "H9", "I9"});
}

void play_to_f7_cut_decision(GameState& state) {
    play_names(state, {"E5"});
    play_names(state, {"D5", "F6"});
    play_names(state, {"D3", "C4"});
    play_names(state, {"B4", "C6"});
    play_names(state, {"C5", "D4"});
    play_names(state, {"B5", "E6", "D6"});
    play_names(state, {"B3", "G6", "H6"});
    play_names(state, {"H8", "H7"});
    play_names(state, {"F5", "G7"});
    play_names(state, {"I7", "G8", "F8"});
}

void play_to_c1_bridge_decision(GameState& state) {
    play_names(state, {"E5"});
    play_names(state, {"E6", "E4"});
    play_names(state, {"F4", "G6"});
    play_names(state, {"G8", "F3"});
    play_names(state, {"E3", "F5"});
    play_names(state, {"D4", "E7", "G7"});
    play_names(state, {"C3", "C4"});
    play_names(state, {"D5", "F6"});
    play_names(state, {"C2", "H7", "H8"});
    play_names(state, {"D2", "D3"});
}

int kind_of(Engine& engine, int cell) {
    int n = engine.move_len[0];
    for (int i = 0; i < n; ++i) {
        if (engine.move_buf[0][i] == cell) return engine.move_kind[0][i];
    }
    CHECK(false);
    return -99;
}

double now() {
    return std::chrono::duration<double>(Steady::now().time_since_epoch()).count();
}

auto run_search(Engine& engine, const GameState& state, double t,
                Engine::ReportFn report = nullptr) {
    GameState copy = state.clone();
    return engine.find_turn(copy, t, std::move(report));
}

void force_side(GameState& state, int side) {
    if (state.to_move == side) return;
    state.to_move = side;
    state.zobrist ^= ZOBRIST_SIDE;
}

void force_allows_three(GameState& state, bool three) {
    if (state.allows_three == three) return;
    state.allows_three = three;
    state.zobrist ^= ZOBRIST_ALLOWS_THREE;
}

void settle_side(GameState& state, int side) {
    force_side(state, side);
    state.placed_this_turn = 0;
    state.max_this_turn = 2;
    force_allows_three(state, false);
    state.score_increased[0] = state.score_increased[1] = false;
    state.score_at_turn_start = state.largest[side];
}

void place_as(GameState& state, int cell, int player) {
    force_side(state, player);
    state.place(cell);
}

// One enclosed white empty and five enclosed black empties. White grows to
// 30 stones so its pocket is larger, but not by enough to be a proven win
// before the race term (30 is not greater than 25 + 6 empties).
GameState split_pockets() {
    bool hole[NUM_CELLS] = {};
    int white_hole = name_to_index("A3");
    int black_hole[5] = {
        name_to_index("G7"), name_to_index("H7"), name_to_index("I7"),
        name_to_index("H8"), name_to_index("I8"),
    };
    hole[white_hole] = true;
    std::uint64_t black_bits = 0;
    for (int h : black_hole) {
        hole[h] = true;
        black_bits |= CELL_BIT[h];
    }
    int color[NUM_CELLS];
    bool frozen[NUM_CELLS] = {};
    for (int c = 0; c < NUM_CELLS; ++c) color[c] = hole[c] ? -1 : BLACK;
    int ncount = NEIGHBOUR_COUNT[white_hole];
    for (int i = 0; i < ncount; ++i) {
        int nb = NEIGHBOURS[white_hole][i];
        if (!hole[nb]) color[nb] = WHITE;
    }
    for (int c = 0; c < NUM_CELLS; ++c) {
        if (color[c] < 0) continue;
        if (NEIGHBOUR_MASK[c] & black_bits) {
            color[c] = BLACK;
            frozen[c] = true;
        }
    }
    int white_n = 0;
    int queued[NUM_CELLS];
    int qh = 0;
    int qt = 0;
    bool seen[NUM_CELLS] = {};
    for (int c = 0; c < NUM_CELLS; ++c) {
        if (color[c] != WHITE) continue;
        white_n += 1;
        queued[qt++] = c;
        seen[c] = true;
    }
    while (qh < qt && white_n < 30) {
        int cell = queued[qh++];
        int nc = NEIGHBOUR_COUNT[cell];
        for (int i = 0; i < nc; ++i) {
            int nb = NEIGHBOURS[cell][i];
            if (seen[nb] || hole[nb] || frozen[nb] || color[nb] != BLACK) continue;
            color[nb] = WHITE;
            seen[nb] = true;
            white_n += 1;
            queued[qt++] = nb;
            if (white_n >= 30) break;
        }
    }
    GameState state;
    for (int c = 0; c < NUM_CELLS; ++c) {
        if (color[c] < 0) continue;
        place_as(state, c, color[c]);
    }
    return state;
}

Engine weights_engine(int w_largest, int w_gift) {
    return Engine(false, false, false, false, false, false, false, 1, 120,
                  w_largest, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                  false, false, false, w_gift, 0);
}

// Ahead side to move, every empty grows it, and the static mate checks do
// not fire. best_opp > 0 so the gift penalty is a nonzero score change.
bool open_forced_gift(const GameState& state) {
    if (state.placed_this_turn != 0 || state.to_move != WHITE) return false;
    if (state.empty_count <= 0) return false;
    std::uint64_t empty_bb = FULL_MASK ^ (state.bb[0] | state.bb[1]);
    if (empty_bb & ~state.touch[WHITE]) return false;
    int min_gain = NUM_CELLS;
    int best_opp = 0;
    std::uint64_t cand = empty_bb;
    while (cand) {
        std::uint64_t bit = cand & -cand;
        int cell = ctz(bit);
        cand ^= bit;
        int gain = placement_gain(state, cell, WHITE);
        if (gain < min_gain) min_gain = gain;
        int opp = placement_gain(state, cell, BLACK);
        if (opp > best_opp) best_opp = opp;
    }
    if (min_gain <= 0 || best_opp <= 0) return false;
    if (state.largest[WHITE] + min_gain < state.largest[BLACK]) return false;
    int mine = state.largest[WHITE];
    int theirs = state.largest[BLACK];
    int empty = state.empty_count;
    if (mine > theirs + empty && mine > state.max_reachable(BLACK)) return false;
    if (theirs > mine + empty && theirs > state.max_reachable(WHITE)) return false;
    int pen = 80 * best_opp * empty / 40;
    if (empty <= 14 || empty > 45) pen /= 2;
    else if (mine - theirs <= -2) pen /= 3;
    else if (mine - theirs <= 1) pen /= 2;
    if (pen < 0) pen = -pen;
    // Same integer soften as the eval. A 1-point gift vanishes against w_largest.
    int softened = pen * 100 / (100 + pen);
    return softened > 0;
}

}  // namespace

void run_search_tests() {
    {
        Engine engine;
        GameState state;
        auto result = engine.find_turn(state, 1.0);
        CHECK(result.first == std::vector<int>({CENTRE_CELL}));
    }

    {
        Engine engine = plain_engine(true, 2);
        for (auto spec : {std::pair<int, int>{0, 0}, {8, 1}, {20, 2}, {40, 3}}) {
            GameState state = spec.first ? random_position(spec.second, spec.first)
                                         : GameState();
            if (state.is_over()) continue;
            auto result = run_search(engine, state, 2.0);
            auto turn = result.first;
            CHECK_GE(static_cast<int>(turn.size()), 1);
            CHECK_LE(static_cast<int>(turn.size()), state.max_this_turn);
            std::set<int> uniq(turn.begin(), turn.end());
            CHECK_EQ(uniq.size(), turn.size());
            for (int cell : turn) CHECK(!state.colour[cell]);
            GameState copy = state.clone();
            copy.play_turn(turn);
            for (int cell : turn) CHECK(copy.colour[cell]);
        }
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine(true, true, true, true, true, true, false);
        auto result = engine.find_turn(state, 0.01);
        CHECK_GE(static_cast<int>(result.first.size()), 1);
        CHECK_LE(static_cast<int>(result.first.size()), state.max_this_turn);
        state.clone().play_turn(result.first);
        CHECK_GT(result.second.elapsed, 0.0);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine(true, true, true, true, true, true, false);
        auto result = engine.find_turn(state, 0.05);
        CHECK(!result.first.empty());
        CHECK_GE(result.second.completed, 1);
        CHECK(result.second.turn == result.first);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine(false, false, true, true, true, true, false);
        engine.st = &state;
        engine.deadline.store(now() - 1.0);
        engine.nodes = 2047;
        bool threw = false;
        try {
            engine._negamax(4, -1000000000, 1000000000, state.max_this_turn, 0, 0, 0, 0);
        } catch (const TimeoutException&) {
            threw = true;
        }
        CHECK(threw);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        auto colour = std::vector<std::uint8_t>(state.colour, state.colour + NUM_CELLS);
        auto z = state.zobrist;
        auto empty = state.empty_count;
        auto to_move = state.to_move;
        auto bb0 = state.bb[0], bb1 = state.bb[1];
        Engine engine(true, true, true, true, true, true, false);
        auto result = engine.find_turn(state, 0.02);
        CHECK(std::vector<std::uint8_t>(state.colour, state.colour + NUM_CELLS) == colour);
        CHECK_EQ(state.zobrist, z);
        CHECK_EQ(state.empty_count, empty);
        CHECK_EQ(state.to_move, to_move);
        CHECK_EQ(state.bb[0], bb0);
        CHECK_EQ(state.bb[1], bb1);
        state.play_turn(result.first);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1", "B1"});
        play_names(state, {"E4", "F5"});
        int depth = 2;
        Engine with_tt = plain_engine(true, depth);
        Engine without = plain_engine(false, depth);
        auto a = run_search(with_tt, state, 30.0);
        auto b = run_search(without, state, 30.0);
        CHECK_EQ(a.second.completed, depth);
        CHECK_EQ(b.second.completed, depth);
        CHECK_EQ(a.second.value, b.second.value);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1", "B1"});
        play_names(state, {"E4", "F5"});
        Engine engine = plain_engine(true, 2);
        run_search(engine, state, 30.0);
        engine.st = &state;
        int depth, value, flag;
        std::vector<int> stored;
        CHECK(engine.tt_lookup(engine._tt_key(&state), depth, value, flag, stored));
        CHECK_GE(depth, 1);
        CHECK(!stored.empty());
        for (int cell : stored) CHECK(!state.colour[cell]);
    }

    {
        Engine engine;
        engine._write_tt(123, 1, 0, EXACT, std::vector<int>{});
        int d, v, f;
        std::vector<int> t;
        CHECK(engine.tt_lookup(123, d, v, f, t));
        engine.history[0][0][0] = 99;
        engine.new_game();
        CHECK(!engine.tt_lookup(123, d, v, f, t));
        CHECK_EQ(engine.history[0][0][0], 0);
    }

    {
        Engine engine;
        std::uint64_t key = 7;
        engine._write_tt(key, 4, 100, EXACT, std::vector<int>{3});
        engine._write_tt(key, 1, 50, EXACT, std::vector<int>{5});
        int depth, value, flag;
        std::vector<int> turn;
        CHECK(engine.tt_lookup(key, depth, value, flag, turn));
        CHECK_EQ(depth, 4);
        CHECK_EQ(value, 100);
        CHECK(turn == std::vector<int>({3}));
        int idx = engine.tt_index(key);
        CHECK_EQ(engine.tt_a[idx].depth, 1);
        CHECK_EQ(engine.tt_a[idx].value, 50);
    }

    {
        Engine engine;
        std::uint64_t k1 = 0;
        std::uint64_t k2 = TT_BUCKETS;
        engine._write_tt(k1, 4, 100, EXACT, std::vector<int>{1});
        engine._write_tt(k2, 1, 50, EXACT, std::vector<int>{2});
        int d1, v1, f1, d2, v2, f2;
        std::vector<int> t1, t2;
        CHECK(engine.tt_lookup(k1, d1, v1, f1, t1));
        CHECK(engine.tt_lookup(k2, d2, v2, f2, t2));
        CHECK_EQ(d1, 4);
        CHECK_EQ(v1, 100);
        CHECK(t1 == std::vector<int>({1}));
        CHECK_EQ(d2, 1);
        CHECK_EQ(v2, 50);
        CHECK(t2 == std::vector<int>({2}));
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1"});
        Engine engine(true, true, false, false);
        engine.st = &state;
        int hash_move = name_to_index("I8");
        auto ordered = engine._ordered_cells(0, hash_move, 0);
        CHECK_EQ(ordered[0], hash_move);
        auto bucket = [&](int cell) {
            if (NEIGHBOUR_MASK[cell] & state.bb[state.to_move]) return 0;
            if (NEIGHBOUR_MASK[cell] & state.bb[1 - state.to_move]) return 1;
            return 2;
        };
        std::vector<int> ranks;
        for (std::size_t i = 1; i < ordered.size(); ++i) ranks.push_back(bucket(ordered[i]));
        auto sorted = ranks;
        std::sort(sorted.begin(), sorted.end());
        CHECK(ranks == sorted);
        int e4 = name_to_index("E4");
        // A2 is the reflection of B1; only the canonical cell is generated.
        int b1 = name_to_index("B1");
        int i9 = name_to_index("I9");
        auto idx_of = [&](int c) {
            return static_cast<int>(std::find(ordered.begin(), ordered.end(), c) - ordered.begin());
        };
        CHECK_LT(idx_of(e4), idx_of(b1));
        CHECK_LT(idx_of(b1), idx_of(i9));
        int a2 = name_to_index("A2");
        int me = state.to_move;
        int opp = 1 - me;
        int a2_kind = engine._cell_kind(a2, me, opp);
        engine._fill_moves(0, a2, 0);
        CHECK_EQ(engine.move_buf[0][0], a2);
        CHECK_EQ(engine.move_kind[0][0], a2_kind);
    }

    {
        GameState state;
        Engine engine(true, true, false, false);
        engine.st = &state;
        int lower = name_to_index("C3");
        auto ordered = engine._ordered_cells(lower, -1, 0);
        for (int c : ordered) CHECK_GE(c, lower);
        int expect = 0;
        for (int c = 0; c < NUM_CELLS; ++c) if (c >= lower) ++expect;
        CHECK_EQ(static_cast<int>(ordered.size()), expect);
    }
    CHECK_EQ(END_TURN, -1);

    {
        Engine engine;
        GameState empty;
        engine.st = &empty;
        CHECK_EQ(engine._evaluate(0), 0);
        CHECK_GT(engine._evaluate(2), engine._evaluate(0));
        CHECK_EQ(empty.max_reachable(WHITE), NUM_CELLS);
        CHECK_EQ(empty.max_reachable(BLACK), NUM_CELLS);
        CHECK_EQ(empty.largest_reachable(WHITE), NUM_CELLS);
        CHECK_EQ(empty.largest_reachable(BLACK), NUM_CELLS);
    }

    {
        Engine engine(true, true, true, true, true, true, true, 64, 120,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      true, true, false, 0, 10);
        GameState leader;
        auto place_side = [&](int side, std::initializer_list<const char*> names) {
            force_side(leader, side);
            for (auto n : names) leader.place(name_to_index(n));
        };
        place_side(WHITE, {"E5", "E4", "E6"});
        place_side(BLACK, {"A1", "I9"});
        place_side(WHITE, {"A3", "C1", "G3", "I5", "C7", "G7",
                          "E1", "E9", "A5", "I7", "C3"});
        force_side(leader, WHITE);
        CHECK_GE(leader.largest[WHITE], leader.largest[BLACK]);
        CHECK_LE(leader.empty_count, 45);

        int my_r = leader.largest_reachable(WHITE);
        int opp_r = leader.largest_reachable(BLACK);
        int block = -1;
        GameState blocked;
        for (int c = 0; c < NUM_CELLS; ++c) {
            if (leader.colour[c]) continue;
            GameState probe = leader.clone();
            force_side(probe, WHITE);
            probe.place(c);
            force_side(probe, WHITE);
            if (probe.largest[WHITE] != leader.largest[WHITE]) continue;
            if (probe.largest[BLACK] != leader.largest[BLACK]) continue;
            if (probe.largest_reachable(WHITE) != my_r) continue;
            if (probe.largest_reachable(BLACK) >= opp_r) continue;
            block = c;
            blocked = std::move(probe);
            break;
        }
        CHECK_GE(block, 0);
        CHECK_LT(blocked.largest_reachable(BLACK), opp_r);

        int w = (opp_r >= leader.largest[WHITE]) ? 10 : REACHABLE_DEAD_WEIGHT;
        engine.st = &leader;
        int open_score = engine._evaluate(0);
        engine.st = &blocked;
        int blocked_score = engine._evaluate(0);
        CHECK_GT(blocked_score, open_score);
        CHECK_EQ(blocked_score - open_score,
                 w * (opp_r - blocked.largest_reachable(BLACK)));

        force_side(leader, BLACK);
        force_side(blocked, BLACK);
        engine.st = &leader;
        int black_open = engine._evaluate(0);
        engine.st = &blocked;
        CHECK_LT(engine._evaluate(0), black_open);
    }

    {
        Engine engine(true, true, true, true, true, true, true, 64, 120,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 0, 0);
        GameState joined;
        force_side(joined, WHITE);
        joined.place(name_to_index("D4"));
        joined.place(name_to_index("F5"));
        force_side(joined, BLACK);
        joined.place(name_to_index("A1"));
        force_side(joined, WHITE);

        GameState isolated;
        force_side(isolated, WHITE);
        isolated.place(name_to_index("E1"));
        isolated.place(name_to_index("I9"));
        force_side(isolated, BLACK);
        isolated.place(name_to_index("A1"));
        force_side(isolated, WHITE);

        std::uint64_t empty_joined = FULL_MASK ^ (joined.bb[0] | joined.bb[1]);
        CHECK_GT(popcnt(joined.join[WHITE] & empty_joined), 0);
        std::uint64_t empty_iso = FULL_MASK ^ (isolated.bb[0] | isolated.bb[1]);
        CHECK_EQ(popcnt(isolated.join[WHITE] & empty_iso), 0);
        engine.st = &joined;
        int join_score = engine._evaluate(0);
        engine.st = &isolated;
        CHECK_GT(join_score, engine._evaluate(0));
    }

    {
        Engine engine;
        GameState bigger;
        play_names(bigger, {"E5"});
        play_names(bigger, {"A1", "I9"});
        bigger.place(name_to_index("E4"));
        bigger.place(name_to_index("F5"));
        engine.st = &bigger;
        CHECK_GT(engine._evaluate(0), 0);
    }

    {
        Engine engine;
        GameState state = random_position(21, -1, false);
        engine.st = &state;
        int positional = std::abs(engine._evaluate(0));
        int terminal = std::abs(state.terminal_score(state.to_move));
        if (terminal) {
            CHECK_GT(terminal, positional);
            CHECK_EQ(terminal, WIN_SCORE);
        }
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine = plain_engine(true, 2);
        auto result = engine.find_turn(state, 10.0);
        CHECK_EQ(result.second.completed, 2);
        CHECK_GT(result.second.nodes, 0);
        CHECK_GT(result.second.nps(), 0.0);
    }

    {
        Engine engine(true, true, true, true, true, true, true, 3);
        GameState state;
        engine.new_game();
        int moves = 0;
        while (!state.is_over()) {
            auto before = std::vector<std::uint8_t>(state.colour, state.colour + NUM_CELLS);
            auto result = engine.find_turn(state, 0.05);
            CHECK(std::vector<std::uint8_t>(state.colour, state.colour + NUM_CELLS) == before);
            state.play_turn(result.first);
            moves += 1;
        }
        CHECK_EQ(state.empty_count, 0);
        CHECK_GT(moves, 20);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1", "B1"});
        for (bool flag : {true, false}) {
            Engine engine(true, true, true, true, true, flag, false, 4);
            auto result = run_search(engine, state, 1.0);
            CHECK_GE(static_cast<int>(result.first.size()), 1);
            CHECK_LE(static_cast<int>(result.first.size()), state.max_this_turn);
            state.clone().play_turn(result.first);
            CHECK_GT(result.second.completed, 0);
        }
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1"});
        state.place(name_to_index("E4"));
        Engine engine(true, true, false, false);
        engine.st = &state;
        int n = engine._fill_actions(0, -1, 0, state.placements_left(),
                                     state.placed_this_turn);
        CHECK_GE(n, 2);
        CHECK_GT(state.largest[0], state.score_at_turn_start);
        int end_at = -1;
        int merge_or_block_before = 0;
        for (int i = 0; i < n; ++i) {
            if (engine.move_buf[0][i] == END_TURN) end_at = i;
            else if (end_at < 0 && (engine.move_kind[0][i] == KIND_MERGE
                                    || engine.move_kind[0][i] == KIND_BLOCK)) {
                merge_or_block_before += 1;
            }
        }
        CHECK_GE(end_at, 0);
        CHECK_NE(engine.move_buf[0][0], END_TURN);
        CHECK_GT(merge_or_block_before, 0);
        CHECK_EQ(engine.move_kind[0][end_at], KIND_END_TURN);
        if (end_at + 1 < n) {
            CHECK_NE(engine.move_kind[0][end_at + 1], KIND_MERGE);
            CHECK_NE(engine.move_kind[0][end_at + 1], KIND_BLOCK);
        }
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1", "I9"});
        play_names(state, {"E4", "D5"});
        CHECK_GT(state.score(WHITE), state.score(BLACK));
        state.place(name_to_index("A2"));
        Engine engine(true, true, false, false);
        engine.st = &state;
        int n = engine._fill_actions(0, -1, 0, state.placements_left(),
                                     state.placed_this_turn);
        bool found = false;
        for (int i = 0; i < n; ++i) if (engine.move_buf[0][i] == END_TURN) found = true;
        CHECK(!found);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1", "I9"});
        play_names(state, {"E4", "D5"});
        state.place(name_to_index("E1"));
        CHECK_EQ(state.largest[BLACK], 1);
        Engine engine(true, true, false, false);
        engine.st = &state;
        int n = engine._fill_actions(0, -1, 0, state.placements_left(),
                                     state.placed_this_turn);
        bool found = false;
        for (int i = 0; i < n; ++i) {
            if (engine.move_buf[0][i] == END_TURN) found = true;
        }
        CHECK(!found);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        state.place(name_to_index("D3"));
        CHECK_EQ(state.largest[BLACK], 1);
        CHECK_EQ(state.largest[BLACK], state.score_at_turn_start);
        Engine engine(true, true, false, false);
        engine.st = &state;
        int n = engine._fill_actions(0, -1, 0, state.placements_left(),
                                     state.placed_this_turn);
        CHECK_GE(n, 2);
        bool has_end = false;
        bool has_quiet = false;
        for (int i = 0; i < n; ++i) {
            if (engine.move_buf[0][i] == END_TURN) has_end = true;
            if (engine.move_kind[0][i] == KIND_QUIET) has_quiet = true;
        }
        CHECK(!has_end);
        CHECK_NE(engine.move_buf[0][0], END_TURN);
        CHECK(has_quiet);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        CHECK_EQ(state.max_this_turn, 2);
        Engine engine(true, true, true, true, true, true, false);
        auto result = run_search(engine, state, 0.4);
        int n = static_cast<int>(result.first.size());
        CHECK(n == 1 || n == 2);
        state.clone().play_turn(result.first);
    }

    {
        GameState merge;
        force_side(merge, BLACK);
        merge.place(name_to_index("A1"));
        merge.place(name_to_index("C1"));
        CHECK_EQ(merge.largest[BLACK], 1);
        CHECK_GE(merge.max_reachable(BLACK), 3);
        Engine engine;
        engine.st = &merge;
        merge.largest[WHITE] = 8;
        merge.empty_count = 3;
        force_side(merge, BLACK);
        CHECK_NE(engine._evaluate(0), -WIN_SCORE);
    }

    {
        GameState won;
        force_side(won, WHITE);
        int a1 = name_to_index("A1");
        int i8 = name_to_index("I8");
        int i9 = name_to_index("I9");
        for (int c = 0; c < NUM_CELLS; ++c) {
            if (c == a1 || c == i8 || c == i9) continue;
            won.place(c);
        }
        force_side(won, BLACK);
        won.place(a1);
        CHECK_EQ(won.empty_count, 2);
        CHECK_GT(won.largest[WHITE], won.largest[BLACK] + won.empty_count);
        CHECK_GT(won.largest[WHITE], won.max_reachable(BLACK));
        Engine engine;
        engine.st = &won;
        force_side(won, WHITE);
        CHECK_EQ(engine._evaluate(0), WIN_SCORE);
        force_side(won, BLACK);
        CHECK_EQ(engine._evaluate(0), -WIN_SCORE);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1", "B1"});
        Clock clk(600.0);
        state.max_this_turn = 2;
        double two = clk.allocate(state);
        state.max_this_turn = 3;
        double three = clk.allocate(state);
        CHECK_GT(three, two);
    }

    {
        GameState quiet;
        play_names(quiet, {"E5"});
        Clock clk(600.0);
        CHECK_LE(merge_volatility(quiet), 1);
        double opening = clk.allocate(quiet);
        CHECK_LT(opening, 90.0);
        CHECK_GT(opening, 0.2);
        int quiet_turns = std::max(1, static_cast<int>(quiet.empty_count / 3.6));
        double even = clk.remaining() / quiet_turns;
        CHECK_LT(opening, even);
        CHECK_GT(opening, even * 0.7);

        GameState joined;
        force_side(joined, WHITE);
        joined.place(name_to_index("D4"));
        joined.place(name_to_index("F5"));
        force_side(joined, BLACK);
        joined.place(name_to_index("A1"));
        force_side(joined, WHITE);
        joined.placed_this_turn = 0;
        CHECK_GE(merge_volatility(joined), 2);
        CHECK_GT(clk.allocate(joined), opening);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Clock clk(600.0);
        clk.used = 560.0;
        state.empty_count = 3;
        double slice = clk.allocate(state);
        double spendable = clk.remaining();
        double wall = clk.total - clk.used;
        CHECK_GT(slice, spendable);
        CHECK_GT(slice, 0.05);
        CHECK_LE(slice, wall - 1.0 + 1e-9);
        CHECK_GE(slice, wall - 1.01);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Clock clk(600.0);
        state.empty_count = 20;
        state.max_this_turn = 2;
        double quiet = clk.allocate(state);
        double spendable = clk.remaining();
        int turns = std::max(1, static_cast<int>(state.empty_count / 3.6));
        double even = spendable / turns;
        CHECK_LT(quiet, even);
        CHECK_GT(quiet, even * 0.7);

        state.max_this_turn = 3;
        double critical = clk.allocate(state);
        CHECK_GT(critical, 0.28 * spendable);
        CHECK_LE(critical, 0.40 * spendable + 1e-6);
        CHECK_GT(critical, quiet);
    }

    {
        GameState state;
        state.empty_count = 3;
        Clock clk(600.0);
        clk.used = 599.2;
        double slice = clk.allocate(state);
        CHECK_GT(slice, 0.05);
        CHECK_LE(slice, 0.15);
    }

    {
        GameState state;
        Clock seeded(600.0);
        double before = seeded.allocate(state);
        Clock learned(600.0);
        for (int i = 0; i < 6; ++i) {
            learned.note_own_stones(2);
            learned.note_reply_stones(1);
        }
        CHECK_LT(learned.avg_cycle, 3.6);
        CHECK_LT(learned.allocate(state), before);
        learned.undo_note();
        learned.undo_note();
        CHECK_LT(learned.allocate(state), before);
        for (int i = 0; i < 10; ++i) learned.undo_note();
        CHECK_EQ(learned.cycle_samples, 1);
        CHECK_EQ(learned.pending_own, -1);
        double restored = learned.allocate(state);
        CHECK_LT(std::abs(restored - before), 1e-9);
    }

    {
        GameState state = random_position(42, 55);
        CHECK_LE(state.empty_count, 8);
        int reports = 0;
        int deepest = 0;
        Engine engine(true, true, true, true, true, true, false, 16);
        auto result = engine.find_turn(state, 3.0, [&](int depth, int, const std::vector<int>&, int, double) {
            reports += 1;
            deepest = depth;
        });
        CHECK_GE(reports, 2);
        CHECK_GE(deepest, 2);
        CHECK_GE(result.second.completed, 1);
        CHECK_GE(static_cast<int>(result.first.size()), 1);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1"});
        state.place(name_to_index("E4"));
        Engine engine(true, true, false, false);
        engine.st = &state;
        auto ordered = engine._ordered_cells(0, -1, 0);
        int f5 = name_to_index("F5");
        int i9 = name_to_index("I9");
        auto idx_of = [&](int c) {
            return static_cast<int>(std::find(ordered.begin(), ordered.end(), c) - ordered.begin());
        };
        CHECK_LT(idx_of(f5), idx_of(i9));
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine(true, true, true, true, true, true, false, 2);
        engine.killer0[80] = 4;
        engine.killer1[80] = 5;
        run_search(engine, state, 0.2);
        CHECK_EQ(engine.killer0[80], 4);
        CHECK_EQ(engine.killer1[80], 5);
        engine.new_game();
        CHECK_EQ(engine.killer0[80], -1);
        CHECK_EQ(engine.killer1[80], -1);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1"});
        Engine engine(true, true, true, false);
        engine.use_countermove = false;
        engine.st = &state;
        engine._fill_moves(0, -1, 0);
        int merge = -1;
        int quiet = -1;
        for (int i = 0; i < engine.move_len[0]; ++i) {
            if (merge < 0 && engine.move_kind[0][i] == KIND_MERGE)
                merge = engine.move_buf[0][i];
            if (engine.move_kind[0][i] == KIND_QUIET)
                quiet = engine.move_buf[0][i];
        }
        CHECK_GE(merge, 0);
        CHECK_GE(quiet, 0);
        engine.killer0[0] = quiet;
        engine._fill_moves(0, -1, 0);
        int merge_at = -1;
        int killer_at = -1;
        for (int i = 0; i < engine.move_len[0]; ++i) {
            if (engine.move_buf[0][i] == merge) merge_at = i;
            if (engine.move_buf[0][i] == quiet) killer_at = i;
        }
        CHECK_GE(merge_at, 0);
        CHECK(kind_is_merge(engine.move_kind[0][merge_at]));
        CHECK_GE(killer_at, 0);
        CHECK_EQ(engine.move_kind[0][killer_at], KIND_QUIET);
        // A legal killer is pulled in front of non-critical moves.
        CHECK_LT(killer_at, merge_at);
        for (int i = 0; i < killer_at; ++i) {
            CHECK_NE(engine.move_kind[0][i], KIND_QUIET);
        }
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1"});
        Engine engine(true, true, true, false);
        engine.st = &state;
        engine._fill_moves(0, -1, 0);
        int merge = -1;
        int quiets[2] = {-1, -1};
        int nq = 0;
        for (int i = 0; i < engine.move_len[0]; ++i) {
            if (merge < 0 && engine.move_kind[0][i] == KIND_MERGE)
                merge = engine.move_buf[0][i];
            if (engine.move_kind[0][i] == KIND_QUIET && nq < 2)
                quiets[nq++] = engine.move_buf[0][i];
        }
        CHECK_GE(merge, 0);
        CHECK_EQ(nq, 2);
        int prev = name_to_index("B1");
        engine.ply_prev[0] = prev;
        engine.counter_move[prev] = quiets[0];
        engine.killer0[0] = quiets[1];
        engine._fill_moves(0, -1, 0);
        int merge_at = -1;
        int cm_at = -1;
        int killer_at = -1;
        for (int i = 0; i < engine.move_len[0]; ++i) {
            if (engine.move_buf[0][i] == merge) merge_at = i;
            if (engine.move_buf[0][i] == quiets[0]) cm_at = i;
            if (engine.move_buf[0][i] == quiets[1]) killer_at = i;
        }
        CHECK_GE(merge_at, 0);
        CHECK_GE(cm_at, 0);
        CHECK_GE(killer_at, 0);
        CHECK_LT(killer_at, cm_at);
        CHECK_LT(cm_at, merge_at);
        CHECK_EQ(engine.move_kind[0][cm_at], KIND_QUIET);
        CHECK_EQ(engine.move_kind[0][killer_at], KIND_QUIET);
        for (int i = 0; i < killer_at; ++i) {
            CHECK_NE(engine.move_kind[0][i], KIND_QUIET);
        }
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1"});
        Engine engine;
        engine.st = &state;
        auto key0 = engine._tt_key(&state);
        state.placed_this_turn = 1;
        auto key1 = engine._tt_key(&state);
        state.placed_this_turn = 2;
        auto key2 = engine._tt_key(&state);
        CHECK_NE(key0, key1);
        CHECK_NE(key1, key2);
        CHECK_EQ(key1 ^ key2, ZOBRIST_PLACED[1] ^ ZOBRIST_PLACED[2]);

        state.placed_this_turn = 1;
        int start = state.score_at_turn_start;
        auto score_a = engine._tt_key(&state, 0);
        state.score_at_turn_start = start == 1 ? 2 : 1;
        auto score_b = engine._tt_key(&state, 0);
        CHECK_NE(score_a, score_b);
        CHECK_EQ(score_a ^ score_b,
                 ZOBRIST_SCORE_START[start] ^ ZOBRIST_SCORE_START[state.score_at_turn_start]);
        state.score_at_turn_start = start;

        auto min_a = engine._tt_key(&state, 6);
        auto min_b = engine._tt_key(&state, 11);
        CHECK_NE(min_a, min_b);
        CHECK_EQ(min_a ^ min_b, ZOBRIST_MIN_INDEX[6] ^ ZOBRIST_MIN_INDEX[11]);
    }

    {
        GameState state = random_position(42, 55);
        CHECK_LE(state.empty_count, 8);
        CHECK_GT(state.empty_count, 0);
        Engine engine(true, true, true, true, true, true, false, 16);
        auto before = std::vector<std::uint8_t>(state.colour, state.colour + NUM_CELLS);
        auto result = engine.find_turn(state, 2.0);
        CHECK(std::vector<std::uint8_t>(state.colour, state.colour + NUM_CELLS) == before);
        CHECK_GE(static_cast<int>(result.first.size()), 1);
        CHECK_LE(static_cast<int>(result.first.size()), state.max_this_turn);
        state.clone().play_turn(result.first);
        // A root cutoff (largest already uncatchable) can finish with 0 nodes.
        CHECK(result.second.nodes > 0 || std::abs(result.second.value) >= WIN_SCORE);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine(true, true, true, true, true, true, false);
        auto result = engine.find_turn(state, 0.2);
        CHECK_GE(result.second.completed, 2);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1", "H9"});
        CHECK_EQ(state.largest[BLACK], 1);
        CHECK_EQ(state.largest_root[BLACK], -1);
        CHECK_GT(state.empty_count, LATE_MERGE_EMPTY);
        Engine engine(true, true, false, false);
        engine.st = &state;
        int n = engine._fill_moves(0, -1, 0);
        (void)n;
        int a2 = name_to_index("A2");
        int c3 = name_to_index("C3");
        CHECK_EQ(kind_of(engine, a2), KIND_BLOCK);
        CHECK_NE(kind_of(engine, a2), KIND_HARRASS);
        CHECK_EQ(kind_of(engine, c3), KIND_QUIET);
        auto ordered = std::vector<int>(engine.move_buf[0], engine.move_buf[0] + engine.move_len[0]);
        auto idx_of = [&](int c) {
            return static_cast<int>(std::find(ordered.begin(), ordered.end(), c) - ordered.begin());
        };
        CHECK_LT(idx_of(a2), idx_of(c3));
        int me = state.to_move, opp = 1 - me;
        CHECK_EQ(engine._cell_kind(a2, me, opp), KIND_BLOCK);
        CHECK_EQ(engine._cell_kind_find(a2, me, opp), KIND_BLOCK);
        engine._fill_moves(0, a2, 0);
        CHECK_EQ(engine.move_buf[0][0], a2);
        CHECK_EQ(engine.move_kind[0][0], KIND_BLOCK);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1"});
        play_names(state, {"E4", "D5"});
        play_names(state, {"I9"});
        CHECK_EQ(state.to_move, WHITE);
        CHECK_GT(state.score_at_turn_start, state.largest[BLACK]);
        CHECK_GT(state.empty_count, LATE_MERGE_EMPTY);
        Engine engine(true, true, false, false);
        engine.st = &state;
        int f6 = name_to_index("F6");
        engine._fill_moves(0, -1, 0);
        CHECK_EQ(kind_of(engine, f6), KIND_EXPAND);
        state.empty_count = LATE_MERGE_EMPTY;
        engine._fill_moves(0, -1, 0);
        CHECK_EQ(kind_of(engine, f6), KIND_MERGE);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1", "H9"});
        int e4 = name_to_index("E4");
        int f5 = name_to_index("F5");
        int a2 = name_to_index("A2");
        int b1 = name_to_index("B1");
        Engine engine(true, true, false, true);
        engine.st = &state;
        engine._fill_moves(0, -1, 0);
        CHECK_EQ(kind_of(engine, e4), KIND_MERGE);
        CHECK_EQ(kind_of(engine, f5), KIND_MERGE);
        CHECK_EQ(kind_of(engine, a2), KIND_BLOCK);
        CHECK_EQ(kind_of(engine, b1), KIND_BLOCK);
        auto merge_before = engine._ordered_cells(0, -1, 0);
        // re-fill so kind_of still works
        engine._fill_moves(0, -1, 0);
        std::vector<int> merge;
        for (int c : merge_before) {
            if (kind_of(engine, c) == KIND_MERGE) merge.push_back(c);
        }
        auto idx_of = [&](const std::vector<int>& v, int c) {
            return static_cast<int>(std::find(v.begin(), v.end(), c) - v.begin());
        };
        CHECK_LT(idx_of(merge, e4), idx_of(merge, f5));

        engine.history[state.to_move][0][f5] = 1000;
        engine.history[state.to_move][0][a2] = 1000;
        engine._fill_moves(0, -1, 0);
        auto ordered = engine._ordered_cells(0, -1, 0);
        engine._fill_moves(0, -1, 0);
        std::vector<int> merge2, block2;
        for (int c : ordered) {
            int k = kind_of(engine, c);
            if (k == KIND_MERGE) merge2.push_back(c);
            if (k == KIND_BLOCK) block2.push_back(c);
        }
        CHECK_LT(idx_of(merge2, f5), idx_of(merge2, e4));
        CHECK_LT(idx_of(block2, a2), idx_of(block2, b1));
    }

    {
        GameState state;
        auto place_side = [&](int side, std::initializer_list<const char*> names) {
            force_side(state, side);
            for (auto n : names) state.place(name_to_index(n));
        };
        // White size 5 at centre. Black: size-3 on the east edge plus two
        // size-2 groups that meet at empty B1 (A1-A2 and C1-C2).
        place_side(WHITE, {"E5", "E4", "E6", "D5", "F5"});
        place_side(BLACK, {"I5", "I6", "I7", "A1", "A2", "C1", "C2"});
        force_side(state, WHITE);
        state.score_at_turn_start = 0;
        state.placed_this_turn = 0;
        CHECK_EQ(state.largest[BLACK], 3);
        CHECK_EQ(placement_gain(state, name_to_index("B1"), BLACK), 2);
        Engine engine(true, true, false, false);
        engine.st = &state;
        int b1 = name_to_index("B1");
        int me = WHITE, opp = BLACK;
        CHECK_EQ(engine._cell_kind(b1, me, opp), KIND_BLOCK);
        CHECK_EQ(engine._cell_kind_find(b1, me, opp), KIND_BLOCK);
        engine._fill_moves(0, -1, 0);
        CHECK(kind_is_block(kind_of(engine, b1)));
        CHECK_NE(kind_of(engine, b1), KIND_HARRASS);
    }

    {
        Engine engine(true, true, true, true, true, true, true, 64, 120,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 10, 0, 0);
        GameState merge;
        auto place_side = [&](GameState& s, int side, std::initializer_list<const char*> names) {
            force_side(s, side);
            for (auto n : names) s.place(name_to_index(n));
        };
        place_side(merge, WHITE, {"A1", "C1"});
        place_side(merge, BLACK, {"I9"});
        force_side(merge, WHITE);
        GameState grow;
        place_side(grow, WHITE, {"A1"});
        place_side(grow, BLACK, {"I9"});
        force_side(grow, WHITE);
        CHECK_EQ(placement_gain(merge, name_to_index("B1"), WHITE), 2);
        engine.st = &merge;
        int merge_score = engine._evaluate(0);
        engine.st = &grow;
        CHECK_GT(merge_score, engine._evaluate(0));
    }

    {
        GameState state;
        state.largest[WHITE] = 5;
        state.second[WHITE] = 3;
        state.size_n[WHITE][5] = 1;
        state.size_n[WHITE][3] = 1;
        state.size_n[WHITE][2] = 1;
        state.largest[BLACK] = 5;
        state.second[BLACK] = 3;
        state.size_n[BLACK][5] = 1;
        state.size_n[BLACK][3] = 1;
        state.size_n[BLACK][1] = 1;
        CHECK_EQ(group_nth(state, WHITE, 3), 2);
        CHECK_EQ(group_nth(state, BLACK, 3), 1);
        Engine engine(true, true, true, true, true, true, true, 64, 120,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 0);
        engine.st = &state;
        state.empty_count = VEC_EMPTY_START;
        CHECK_EQ(engine._evaluate(0), 0);
        state.empty_count = VEC_EMPTY_START - VEC_PHASE_SPAN;
        CHECK_EQ(engine._evaluate(0), 12);
    }

    {
        GameState state;
        auto place_side = [&](int side, std::initializer_list<const char*> names) {
            force_side(state, side);
            for (auto n : names) state.place(name_to_index(n));
        };
        place_side(WHITE, {"E5", "E4", "E6", "D5", "F5", "A1"});
        place_side(BLACK, {"H9"});
        force_side(state, WHITE);
        state.score_at_turn_start = 0;
        state.placed_this_turn = 0;
        int d4 = name_to_index("D4");
        int a2 = name_to_index("A2");
        CHECK_EQ(state.largest[WHITE], 5);
        CHECK_EQ(placement_gain(state, d4, WHITE), 1);
        CHECK_EQ(placement_gain(state, a2, WHITE), 0);
        Engine engine(true, true, false, false);
        engine.st = &state;
        engine._fill_moves(0, -1, 0);
        CHECK_EQ(kind_of(engine, d4), KIND_MERGE);
        CHECK_EQ(kind_of(engine, a2), KIND_EXPAND);
        auto ordered = engine._ordered_cells(0, -1, 0);
        auto idx_of = [&](int c) {
            return static_cast<int>(std::find(ordered.begin(), ordered.end(), c) - ordered.begin());
        };
        CHECK_LT(idx_of(d4), idx_of(a2));
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine(true, true, false, false);
        engine.st = &state;
        engine._fill_moves(0, -1, 0);
        int n = engine.move_len[0];
        std::set<std::pair<int, int>> pairs;
        for (int i = 0; i < n; ++i) {
            int a = engine.move_buf[0][i];
            state.place(a);
            engine._fill_moves(a + 1, -1, 1);
            int n2 = engine.move_len[1];
            for (int j = 0; j < n2; ++j) {
                int b = engine.move_buf[1][j];
                CHECK_GT(b, a);
                pairs.insert({std::min(a, b), std::max(a, b)});
            }
            state.unplace();
        }
        CHECK_GT(static_cast<int>(pairs.size()), 10);
    }

    {
        Engine engine(true, true, false, false);
        GameState empty;
        engine.st = &empty;
        int live[N_SYMMETRIES];
        CHECK_EQ(engine._live_symmetries(live), N_SYMMETRIES);
        engine._fill_moves(0, -1, 0);
        CHECK_EQ(engine.move_len[0], 9);
        bool has_centre = false;
        for (int i = 0; i < engine.move_len[0]; ++i) {
            if (engine.move_buf[0][i] == CENTRE_CELL) has_centre = true;
        }
        CHECK(has_centre);
        play_names(empty, {"E5"});
        CHECK_EQ(engine._live_symmetries(live), N_SYMMETRIES);
        engine._fill_moves(0, -1, 0);
        CHECK_EQ(engine.move_len[0], 8);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine(true, true, false, false);
        engine.st = &state;
        int cell = name_to_index("A2");
        std::uint64_t pred = engine._place_child_key(cell);
        state.place(cell);
        CHECK_EQ(pred, engine._tt_key(&state, cell + 1));
        std::uint64_t end_pred = engine._end_turn_child_key();
        state.end_turn();
        CHECK_EQ(end_pred, engine._tt_key(&state, 0));
        state.unend_turn();
        state.unplace();
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine(true, false, false, false, false, false, false);
        engine.st = &state;
        int cell = name_to_index("A1");
        engine._write_tt(engine._place_child_key(cell), 8, 777, LOWER_BOUND,
                         std::vector<int>{cell});
        engine.deadline.store(now() + 30.0);
        engine.nodes = 0;
        int value = engine._negamax(4, -INFINITY_SCORE, 100, state.max_this_turn,
                                    0, 1, 0, 0);
        CHECK_EQ(value, 777);
        CHECK_LT(engine.nodes, 30);
    }

    {
        CHECK_EQ(TT_BITS, 20);
        CHECK_EQ(TT_BUCKETS, 1 << 20);
        Engine engine;
        std::uint64_t k1 = 42;
        std::uint64_t k2 = 42 + TT_BUCKETS;
        CHECK_EQ(tt_checksum(k1), k1 >> TT_BITS);
        CHECK_NE(tt_checksum(k1), tt_checksum(k2));
        CHECK_EQ(static_cast<int>(k1 & TT_MASK), static_cast<int>(k2 & TT_MASK));
        engine._write_tt(k1, 5, 80, EXACT, std::vector<int>{4});
        int d, v, f;
        std::vector<int> t;
        CHECK(engine.tt_lookup(k1, d, v, f, t));
        CHECK_EQ(v, 80);
        CHECK(!engine.tt_lookup(k2, d, v, f, t));
        engine._write_tt(k2, 2, 11, EXACT, std::vector<int>{6});
        CHECK(engine.tt_lookup(k1, d, v, f, t));
        CHECK_EQ(v, 80);
        CHECK(engine.tt_lookup(k2, d, v, f, t));
        CHECK_EQ(v, 11);
    }

    {
        Engine engine;
        GameState quiet;
        play_names(quiet, {"E5"});
        engine.st = &quiet;
        CHECK_LE(merge_volatility(quiet), 1);
        CHECK_EQ(engine._seed_aspiration(), 60);

        GameState joined;
        force_side(joined, WHITE);
        joined.place(name_to_index("D4"));
        joined.place(name_to_index("F5"));
        force_side(joined, BLACK);
        joined.place(name_to_index("A1"));
        force_side(joined, WHITE);
        engine.st = &joined;
        CHECK_GE(merge_volatility(joined), 2);
        CHECK_GT(engine._seed_aspiration(), engine.aspiration_delta);
    }

    {
        Engine engine;
        CHECK(engine.use_rfp);
        CHECK(engine.use_lmp);
        CHECK(!engine.use_iid);
    }

    {
        GameState quiet;
        play_names(quiet, {"E5"});
        Engine engine(false, false, false, false, false, false, false);
        engine.use_rfp = true;
        engine.use_lmp = false;
        engine.st = &quiet;
        engine.deadline.store(now() + 30.0);
        CHECK_LE(merge_volatility(quiet), 1);
        CHECK_EQ(quiet.placed_this_turn, 0);
        int eval = engine._evaluate(quiet.max_this_turn);
        int depth = 3;
        int margin = RFP_BASE + RFP_DEPTH * depth;
        int beta = eval - margin;
        engine.nodes = 0;
        int value = engine._negamax(depth, -INFINITY_SCORE, beta,
                                    quiet.max_this_turn, 0, 1, 0, 0);
        CHECK_EQ(value, eval);
        CHECK_EQ(engine.nodes, 1);
    }

    {
        GameState joined;
        force_side(joined, WHITE);
        joined.place(name_to_index("D4"));
        joined.place(name_to_index("F5"));
        force_side(joined, BLACK);
        joined.place(name_to_index("A1"));
        force_side(joined, WHITE);
        joined.placed_this_turn = 0;
        Engine engine(false, false, false, false, false, false, false);
        engine.use_rfp = true;
        engine.use_lmp = false;
        engine.st = &joined;
        engine.deadline.store(now() + 30.0);
        CHECK_GE(merge_volatility(joined), 2);
        CHECK_EQ(joined.placed_this_turn, 0);
        int eval = engine._evaluate(joined.max_this_turn);
        int depth = 3;
        int margin = RFP_BASE + RFP_DEPTH * depth;
        int beta = eval - margin;
        engine.nodes = 0;
        (void)engine._negamax(depth, -INFINITY_SCORE, beta,
                              joined.max_this_turn, 0, 1, 0, 0);
        CHECK_GT(engine.nodes, 1);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        state.place(name_to_index("D3"));
        Engine engine(true, true, false, false);
        engine.st = &state;
        int n = engine._fill_actions(0, -1, 0, state.placements_left(),
                                     state.placed_this_turn);
        int n_quiet = 0;
        for (int i = 0; i < n; ++i) {
            if (engine.move_kind[0][i] == KIND_QUIET) n_quiet += 1;
        }
        CHECK_GT(n_quiet, LMP_QUIET_D2);
        CHECK_NE(engine.move_buf[0][n - 1], END_TURN);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine(true, true, true, true, true, true, false, 6, 120,
                      100, 2, 1, 2, 110, 25, 8, 20, 40, 4, 16, 25, 8,
                      true, true, true);
        CHECK(engine.use_iid);
        auto result = run_search(engine, state, 0.2);
        CHECK_GE(static_cast<int>(result.first.size()), 1);
    }

    {
        Engine engine;
        CHECK_EQ(engine.w_tempo, 0);
        CHECK_EQ(engine.w_gift, 80);
        CHECK_EQ(engine.w_reachable, 10);
        CHECK_EQ(engine.lmr_quiet_cap, 5);
        CHECK(std::abs(engine.lmr_divisor - 1.8) < 1e-12);
        GameState empty;
        CHECK(!exact_solve_position(empty));
        empty.empty_count = ENDGAME_EMPTY;
        CHECK(exact_solve_position(empty));
        empty.empty_count = EXACT_EMPTY_MAX;
        CHECK(exact_solve_position(empty));
        empty.empty_count = EXACT_EMPTY_MAX + 1;
        CHECK(!exact_solve_position(empty));
    }

    {
        Engine engine(true, true, false, false, false, false, false, 64, 120,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      false, false, false, 80, 0);
        GameState spend;
        force_side(spend, WHITE);
        spend.place(name_to_index("D4"));
        spend.place(name_to_index("F5"));
        force_side(spend, BLACK);
        spend.place(name_to_index("A1"));
        force_side(spend, WHITE);
        spend.placed_this_turn = 0;
        force_allows_three(spend, true);
        GameState dead;
        force_side(dead, WHITE);
        dead.place(name_to_index("E1"));
        dead.place(name_to_index("I9"));
        force_side(dead, BLACK);
        dead.place(name_to_index("A1"));
        force_side(dead, WHITE);
        dead.placed_this_turn = 0;
        force_allows_three(dead, true);
        CHECK_GE(best_placement_gain(spend, WHITE), 2);
        CHECK_LT(best_placement_gain(dead, WHITE), 2);
        engine.st = &spend;
        int spend_score = engine._evaluate(3);
        engine.st = &dead;
        CHECK_GT(spend_score, engine._evaluate(3));
        force_allows_three(dead, false);
        CHECK_EQ(engine._evaluate(3), 0);
    }

    {
        GameState ahead;
        force_side(ahead, WHITE);
        ahead.place(name_to_index("A1"));
        ahead.place(name_to_index("C1"));
        force_side(ahead, BLACK);
        ahead.place(name_to_index("I9"));
        force_side(ahead, WHITE);
        ahead.score_at_turn_start = 8;
        ahead.placed_this_turn = 0;
        CHECK_GE(placement_gain(ahead, name_to_index("B1"), WHITE), 2);
        Engine engine(true, true, false, false);
        engine.st = &ahead;
        CHECK_EQ(engine._cell_kind(name_to_index("B1"), WHITE, BLACK), KIND_EXPAND);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"D4", "E6"});
        play_names(state, {"F7", "D3"});
        play_names(state, {"C5", "G5"});
        play_names(state, {"G6", "F4"});
        play_names(state, {"D2", "G8"});
        play_names(state, {"D6", "E2"});
        play_names(state, {"B2", "D7"});
        play_names(state, {"F6", "E4"});
        CHECK_EQ(state.max_this_turn, 3);
        CHECK(state.allows_three);
        Engine engine;
        engine.st = &state;
        int d5 = name_to_index("D5");
        auto completed = engine._sanitise_turn(state, std::vector<int>{d5});
        CHECK_EQ(static_cast<int>(completed.size()), 3);
        CHECK_EQ(completed[0], d5);
        GameState copy = state.clone();
        copy.play_turn(completed);
        CHECK_EQ(copy.nstones[BLACK], state.nstones[BLACK] + 3);
    }

    {
        GameState state;
        play_to_black_gift3_after_white_five(state);
        CHECK_EQ(state.max_this_turn, 3);
        CHECK_EQ(state.largest[WHITE], 5);
        CHECK_EQ(state.largest[BLACK], 1);
        state.place(name_to_index("C3"));
        state.place(name_to_index("B4"));
        CHECK_EQ(state.largest[BLACK], 5);
        CHECK_EQ(state.placed_this_turn, 2);
        Engine engine(true, true, false, false);
        engine.st = &state;
        int n = engine._fill_actions(0, -1, 0, state.placements_left(),
                                     state.placed_this_turn);
        int end_at = -1;
        bool gain_before_end = false;
        int d2 = name_to_index("D2");
        int e4 = name_to_index("E4");
        int c5 = name_to_index("C5");
        for (int i = 0; i < n; ++i) {
            if (engine.move_buf[0][i] == END_TURN) {
                end_at = i;
                break;
            }
            int cell = engine.move_buf[0][i];
            if (cell == d2 || cell == e4 || cell == c5) gain_before_end = true;
        }
        CHECK_GE(end_at, 1);
        CHECK(gain_before_end);
        CHECK_EQ(engine.move_kind[0][end_at], KIND_END_TURN);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"D4", "F6"});
        play_names(state, {"D3", "F7"});
        play_names(state, {"E4", "E6"});
        play_names(state, {"C4", "G6"});
        play_names(state, {"H7", "G8"});
        play_names(state, {"B4", "H6"});
        play_names(state, {"C3", "F5", "G7"});
        play_names(state, {"D5", "D6", "E7"});
        CHECK_EQ(state.max_this_turn, 2);
        CHECK(!state.allows_three);
        CHECK_EQ(state.largest[BLACK], 9);
        CHECK_EQ(state.largest[WHITE], 7);
        Engine engine;
        engine.st = &state;
        int a3 = name_to_index("A3");
        CHECK_EQ(placement_gain(state, a3, BLACK), 0);
        state.place(a3);
        CHECK_EQ(state.largest[BLACK], 9);
        CHECK(!engine._end_turn_allowed(state.placements_left(),
                                        state.placed_this_turn));
        int n = engine._fill_actions(a3 + 1, -1, 0, state.placements_left(),
                                     state.placed_this_turn);
        bool has_end = false;
        for (int i = 0; i < n; ++i) {
            if (engine.move_buf[0][i] == END_TURN) has_end = true;
        }
        CHECK(!has_end);
        state.unplace();
        auto completed = engine._sanitise_turn(state, std::vector<int>{a3});
        CHECK_EQ(static_cast<int>(completed.size()), 2);
        auto result = run_search(engine, state, 0.8);
        CHECK_EQ(static_cast<int>(result.first.size()), 2);
        state.clone().play_turn(result.first);
    }

    {
        GameState lead;
        force_side(lead, WHITE);
        lead.place(name_to_index("E5"));
        lead.place(name_to_index("E4"));
        lead.place(name_to_index("E6"));
        force_side(lead, BLACK);
        lead.place(name_to_index("A1"));
        force_side(lead, WHITE);
        lead.score_at_turn_start = 2;
        lead.placed_this_turn = 0;
        lead.max_this_turn = 2;
        int f5 = name_to_index("F5");
        CHECK_GE(placement_gain(lead, f5, WHITE), 1);
        lead.place(f5);
        CHECK_GT(lead.largest[WHITE], lead.score_at_turn_start);
        CHECK_GE(lead.largest[WHITE], lead.largest[BLACK]);
        Engine engine(true, true, false, false);
        engine.st = &lead;
        CHECK(engine._end_turn_allowed(lead.placements_left(),
                                       lead.placed_this_turn));
        int n = engine._fill_actions(f5 + 1, -1, 0, lead.placements_left(),
                                     lead.placed_this_turn);
        int end_at = -1;
        bool expand_before_end = false;
        for (int i = 0; i < n; ++i) {
            if (engine.move_buf[0][i] == END_TURN) {
                end_at = i;
                break;
            }
            if (engine.move_kind[0][i] == KIND_EXPAND) expand_before_end = true;
        }
        CHECK_GE(end_at, 1);
        CHECK(expand_before_end);
    }

    {
        GameState state;
        play_to_black_gift3_after_white_five(state);
        Engine engine;
        engine.st = &state;
        int c3 = name_to_index("C3");
        int b4 = name_to_index("B4");
        auto completed = engine._sanitise_turn(state, std::vector<int>{c3, b4});
        CHECK_EQ(static_cast<int>(completed.size()), 3);
        GameState copy = state.clone();
        copy.play_turn(completed);
        CHECK_GE(copy.largest[BLACK], 7);
    }

    {
        GameState state;
        play_to_black_gift3_after_white_five(state);
        Engine engine(true, true, true, true, true, true, false, 3);
        auto result = run_search(engine, state, 8.0);
        CHECK_EQ(static_cast<int>(result.first.size()), 3);
        GameState copy = state.clone();
        copy.play_turn(result.first);
        CHECK_GE(copy.largest[BLACK], 6);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        Engine engine;
        auto result = run_search(engine, state, 0.5);
        CHECK_GE(static_cast<int>(result.first.size()), 2);
        CHECK_LE(static_cast<int>(result.first.size()), 2);
        state.clone().play_turn(result.first);
    }

    {
        GameState state;
        play_to_black_gift3_after_white_five(state);
        play_names(state, {"C3", "B4"});
        play_names(state, {"H6", "C5", "A5"});
        CHECK_EQ(state.largest[WHITE], 10);
        CHECK_EQ(state.largest[BLACK], 5);
        Engine engine;
        engine.st = &state;
        int g4 = name_to_index("G4");
        CHECK_GE(own_group_count(state, g4, BLACK), 2);
        CHECK_EQ(placement_gain(state, g4, BLACK), 0);
        CHECK_EQ(engine._cell_kind(g4, BLACK, WHITE), KIND_MERGE);
        int n = engine._fill_moves(0, -1, 0);
        bool g4_merge = false;
        for (int i = 0; i < n; ++i) {
            if (engine.move_buf[0][i] == g4) {
                CHECK_EQ(engine.move_kind[0][i], KIND_MERGE);
                g4_merge = true;
            }
        }
        CHECK(g4_merge);
    }

    {
        GameState state;
        play_to_black_gift3_after_white_five(state);
        play_names(state, {"C3", "B4"});
        play_names(state, {"H6", "C5", "A5"});
        play_names(state, {"D2", "F3", "H8"});
        play_names(state, {"G4", "G3"});
        Engine engine;
        GameState stay = state.clone();
        play_names(stay, {"A4", "E4", "F8"});
        GameState catchup = state.clone();
        play_names(catchup, {"A1", "B1", "C1"});
        engine.st = &stay;
        int stay_white = engine._evaluate(stay.max_this_turn);
        engine.st = &catchup;
        int catch_white = engine._evaluate(catchup.max_this_turn);
        CHECK_GT(stay_white - catch_white, -100);
    }

    {
        CHECK_EQ(end_turn_child_depth(6, true, 4, 3), 0);
        CHECK_EQ(end_turn_child_depth(6, false, 4, 2), 6);
        CHECK_EQ(end_turn_child_depth(6, false, 4, 3), 7);
        CHECK_EQ(end_turn_child_depth(6, false, 4, 1), 6);
        CHECK_EQ(end_turn_child_depth(3, false, 4, 3), 4);
        CHECK_EQ(end_turn_child_depth(2, false, 4, 3), 2);
        CHECK_EQ(end_turn_child_depth(6, false, EXTEND_PLY_CAP, 3), 6);
        CHECK_EQ(end_turn_child_depth(0, false, 2, 3), 0);
        CHECK_EQ(end_turn_child_depth(6, false, 4, 3, true), 6);
    }

    {
        GameState gift;
        play_to_black_gift3_after_white_five(gift);
        gift.place(name_to_index("C3"));
        gift.place(name_to_index("B4"));
        CHECK_EQ(predicted_next_turn_max(gift), 3);
        CHECK_EQ(end_turn_child_depth(6, false, 4, predicted_next_turn_max(gift)), 7);
        gift.end_turn();
        CHECK_EQ(gift.max_this_turn, 3);
        CHECK(gift.allows_three);

        GameState stay;
        play_names(stay, {"E5"});
        stay.place(name_to_index("A1"));
        stay.place(name_to_index("I9"));
        CHECK_EQ(predicted_next_turn_max(stay), 2);
        CHECK_EQ(end_turn_child_depth(6, false, 4, predicted_next_turn_max(stay)), 6);
        stay.end_turn();
        CHECK_EQ(stay.max_this_turn, 2);
        CHECK(!stay.allows_three);
    }

    {
        GameState state;
        play_to_h7_i8_decision(state);
        CHECK_EQ(state.to_move, BLACK);
        CHECK_EQ(state.empty_count, 24);
        CHECK_EQ(state.nstones[WHITE], 19);
        CHECK_EQ(state.nstones[BLACK], 18);
        CHECK_EQ(state.largest[WHITE], 16);
        CHECK_EQ(state.largest[BLACK], 18);
        CHECK(state.group_sizes(WHITE) == std::vector<int>({16, 2, 1}));
        CHECK(state.group_sizes(BLACK) == std::vector<int>({18}));
        CHECK_EQ(state.largest_reachable(WHITE), 22);
        CHECK_EQ(state.largest_reachable(BLACK), 36);
        CHECK_GE(state.largest_reachable(WHITE), state.largest[WHITE]);
        CHECK_LE(state.largest_reachable(WHITE), state.nstones[WHITE] + state.empty_count);

        int h7 = name_to_index("H7");
        int i8 = name_to_index("I8");
        int i7 = name_to_index("I7");
        Engine engine(true, true, false, false);
        engine.st = &state;
        engine._fill_moves(0, -1, 0);
        CHECK_EQ(kind_of(engine, h7), KIND_BLOCK);
        CHECK_EQ(kind_of(engine, i8), KIND_BLOCK);
        CHECK_EQ(engine._cell_kind(h7, BLACK, WHITE), KIND_BLOCK);
        CHECK_EQ(engine._cell_kind(i8, BLACK, WHITE), KIND_BLOCK);

        GameState wall = state.clone();
        play_names(wall, {"H7", "I8"});
        CHECK_EQ(wall.largest[BLACK], 18);
        CHECK_EQ(wall.largest_reachable(WHITE), 20);
        CHECK_EQ(wall.max_this_turn, 2);
        CHECK(!wall.allows_three);

        GameState grow = state.clone();
        play_names(grow, {"F2", "D3"});
        CHECK_EQ(grow.largest[BLACK], 20);
        CHECK(grow.allows_three);
        CHECK_EQ(grow.max_this_turn, 3);
        play_names(grow, {"H7", "I8", "G8"});
        CHECK_EQ(grow.largest[WHITE], 19);
        CHECK_EQ(grow.largest[BLACK], 20);

        GameState dead = state.clone();
        force_side(dead, BLACK);
        for (auto n : {"F2", "D3", "E2", "C2", "E1"}) {
            dead.place(name_to_index(n));
        }
        CHECK_GT(dead.largest[BLACK], dead.largest_reachable(WHITE));
        engine.st = &dead;
        engine._fill_moves(0, -1, 0);
        CHECK_NE(kind_of(engine, h7), KIND_BLOCK);
        CHECK_NE(kind_of(engine, i8), KIND_BLOCK);
        CHECK_NE(engine._cell_kind(h7, BLACK, WHITE), KIND_BLOCK);
        CHECK_NE(engine._cell_kind(i7, BLACK, WHITE), KIND_BLOCK);
    }

    {
        GameState state;
        play_to_f7_cut_decision(state);
        CHECK_EQ(state.to_move, WHITE);
        CHECK_EQ(state.max_this_turn, 2);
        CHECK(state.group_sizes(BLACK) == std::vector<int>({7, 5}));
        int e7 = name_to_index("E7");
        int f7 = name_to_index("F7");
        int e8 = name_to_index("E8");
        CHECK_EQ(placement_gain(state, f7, BLACK), 6);
        CHECK_EQ(placement_gain(state, e7, BLACK), 6);

        Engine engine(true, true, false, false);
        engine.st = &state;
        CHECK_EQ(engine._cell_kind(f7, WHITE, BLACK), KIND_BLOCK);
        CHECK_EQ(engine._cell_kind(e7, WHITE, BLACK), KIND_BLOCK);
        engine._fill_moves(0, -1, 0);
        CHECK(kind_is_block(kind_of(engine, f7)));
        CHECK(kind_is_block(kind_of(engine, e7)));

        GameState cut = state.clone();
        cut.place(e7);
        cut.place(f7);
        GameState miss = state.clone();
        miss.place(e7);
        miss.place(e8);
        engine.st = &cut;
        int cut_score = engine._evaluate(0);
        engine.st = &miss;
        int miss_score = engine._evaluate(0);
        CHECK_GE(cut_score - miss_score, engine.w_largest);

        // Depth 2 is the horizon that sees Black's 1-ply F7 merge without
        // searching the gifted 3-stone reconnect that still fools eval.
        Engine search_engine(true, true, true, true, true, true, false, 2);
        auto result = run_search(search_engine, state, 0.5);
        bool took_f7 = false;
        bool took_e7 = false;
        for (int cell : result.first) {
            if (cell == f7) took_f7 = true;
            if (cell == e7) took_e7 = true;
        }
        CHECK(took_f7);
        CHECK(took_e7);
    }

    {
        GameState two;
        force_side(two, WHITE);
        two.place(name_to_index("A1"));
        two.place(name_to_index("B1"));
        two.place(name_to_index("B2"));
        two.place(name_to_index("E1"));
        two.place(name_to_index("E2"));
        two.place(name_to_index("F2"));
        two.place(name_to_index("F3"));
        force_side(two, BLACK);
        two.place(name_to_index("I9"));
        force_side(two, WHITE);
        two.placed_this_turn = 0;
        two.max_this_turn = 2;
        two.score_at_turn_start = 1;
        CHECK_EQ(two.largest[WHITE], 4);
        CHECK_EQ(two.second[WHITE], 3);
        int c1 = name_to_index("C1");
        int d1 = name_to_index("D1");
        BridgeInfo br = own_bridges(two, WHITE, 2);
        CHECK_NE(br.starters & CELL_BIT[c1], 0);
        CHECK_NE(br.cells & CELL_BIT[c1], 0);
        CHECK_NE(br.cells & CELL_BIT[d1], 0);
        CHECK_GE(br.gain, 5);
        Engine engine(true, true, false, false);
        engine.st = &two;
        CHECK_EQ(engine._cell_kind(c1, WHITE, BLACK), KIND_MERGE);
        int n = engine._fill_moves(0, -1, 0);
        bool c1_merge = false;
        for (int i = 0; i < n; ++i) {
            if (engine.move_buf[0][i] == c1) {
                CHECK_EQ(engine.move_kind[0][i], KIND_CRIT_MERGE);
                c1_merge = true;
            }
        }
        CHECK(c1_merge);
    }

    {
        GameState state;
        play_to_c1_bridge_decision(state);
        CHECK_EQ(state.to_move, WHITE);
        CHECK_EQ(state.max_this_turn, 3);
        CHECK(state.group_sizes(WHITE) == std::vector<int>({7, 3}));
        CHECK_EQ(state.largest[BLACK], 10);
        int c1 = name_to_index("C1");
        int d1 = name_to_index("D1");
        int e2 = name_to_index("E2");
        int h5 = name_to_index("H5");
        BridgeInfo br = own_bridges(state, WHITE, 3);
        CHECK_NE(br.starters & CELL_BIT[c1], 0);
        CHECK_NE(br.cells & CELL_BIT[c1], 0);
        CHECK_NE(br.cells & CELL_BIT[d1], 0);
        CHECK_NE(br.cells & CELL_BIT[e2], 0);
        CHECK_GE(br.gain, 6);

        Engine engine(true, true, false, false);
        engine.st = &state;
        CHECK_EQ(engine._cell_kind(c1, WHITE, BLACK), KIND_MERGE);
        int n0 = engine._fill_moves(0, -1, 0);
        bool c1_merge = false;
        for (int i = 0; i < n0; ++i) {
            if (engine.move_buf[0][i] == c1) {
                CHECK_EQ(engine.move_kind[0][i], KIND_CRIT_MERGE);
                c1_merge = true;
            }
        }
        CHECK(c1_merge);

        GameState mid = state.clone();
        mid.place(d1);
        mid.place(e2);
        engine.st = &mid;
        int n1 = engine._fill_moves(e2 + 1, -1, 0);
        bool c1_after = false;
        for (int i = 0; i < n1; ++i) {
            if (engine.move_buf[0][i] == c1) c1_after = true;
        }
        CHECK(c1_after);

        GameState joined = state.clone();
        joined.place(c1);
        joined.place(d1);
        joined.place(e2);
        GameState missed = state.clone();
        missed.place(d1);
        missed.place(e2);
        missed.place(h5);
        engine.st = &joined;
        int join_score = engine._evaluate(0);
        engine.st = &missed;
        int miss_score = engine._evaluate(0);
        CHECK_GE(join_score - miss_score, engine.w_largest);
        CHECK_EQ(joined.largest[WHITE], 13);
        CHECK_EQ(missed.largest[WHITE], 9);

        Engine search_engine(true, true, true, true, true, true, false, 3);
        auto result = run_search(search_engine, state, 1.0);
        bool took_c1 = false, took_d1 = false, took_e2 = false;
        for (int cell : result.first) {
            if (cell == c1) took_c1 = true;
            if (cell == d1) took_d1 = true;
            if (cell == e2) took_e2 = true;
        }
        CHECK(took_c1);
        CHECK(took_d1);
        CHECK(took_e2);
        GameState copy = state.clone();
        copy.play_turn(result.first);
        CHECK_GE(copy.largest[WHITE], 13);
    }

    {
        Engine engine;
        engine._write_tt(123, 2, 42, EXACT, std::vector<int>{});
        int d, v, f;
        std::vector<int> t;
        CHECK(engine.tt_lookup(123, d, v, f, t));
        std::uint8_t gen = engine.tt_generation;
        engine.new_game();
        CHECK_EQ(engine.tt_generation, static_cast<std::uint8_t>(gen + 1));
        CHECK(!engine.tt_lookup(123, d, v, f, t));
        engine._write_tt(123, 2, 7, EXACT, std::vector<int>{1});
        CHECK(engine.tt_lookup(123, d, v, f, t));
        CHECK_EQ(v, 7);
    }

    {
        Engine small(true, true, true, true, true, true, false, 64, 120,
                     100, 2, 1, 2, 0, 25, 8, 20, 40, 4, 16, 25, 8,
                     true, true, false, 80, 10, 16);
        CHECK_EQ(small.tt_bits, 16);
        CHECK_EQ(small.tt_buckets, 1 << 16);
        std::uint64_t k1 = 9;
        std::uint64_t k2 = 9 + (1u << 16);
        small._write_tt(k1, 4, 30, EXACT, std::vector<int>{2});
        small._write_tt(k2, 1, 5, EXACT, std::vector<int>{3});
        int d, v, f;
        std::vector<int> t;
        CHECK(small.tt_lookup(k1, d, v, f, t));
        CHECK_EQ(v, 30);
        CHECK(small.tt_lookup(k2, d, v, f, t));
        CHECK_EQ(v, 5);
    }

    {
        Engine engine;
        GameState mid = random_position(11, 22);
        engine.find_turn(mid, 0.25);
        int mx = 0;
        for (int p = 0; p < 2; ++p) {
            for (int pl = 0; pl < HIST_PLACED; ++pl) {
                for (int c = 0; c < NUM_CELLS; ++c) {
                    int h = std::abs(engine.history[p][pl][c]);
                    if (h > mx) mx = h;
                }
            }
        }
        CHECK_LT(mx, HIST_MAX);
    }

    {
        Clock clock(600.0, 8.0);
        GameState opening;
        double slice = clock.allocate(opening);
        CHECK_LE(slice, clock.remaining() + 1e-9);
        clock.used = 590.0;
        double late = clock.allocate(opening);
        CHECK_LE(late, clock.remaining() + 1e-9);
        CHECK_GE(late, 0.05);
    }

    {
        CHECK_EQ(ENDGAME_EMPTY, 10);
        CHECK_EQ(EXACT_TACTICAL_MAX, 8);
        GameState empty;
        CHECK(!exact_solve_position(empty));
        empty.empty_count = ENDGAME_EMPTY;
        CHECK(exact_solve_position(empty));
        empty.empty_count = EXACT_EMPTY_MAX + 1;
        CHECK(!exact_solve_position(empty));
    }

    {
        CHECK_EQ(score_to_tt(WIN_SCORE - 5, 5), WIN_SCORE);
        CHECK_EQ(score_from_tt(WIN_SCORE, 9), WIN_SCORE - 9);
        CHECK_EQ(score_from_tt(score_to_tt(WIN_SCORE - 5, 5), 9), WIN_SCORE - 9);
        CHECK_EQ(score_from_tt(score_to_tt(5 - WIN_SCORE, 5), 9), 9 - WIN_SCORE);
        CHECK_EQ(score_to_tt(40, 5), 40);
        CHECK_EQ(score_from_tt(40, 9), 40);

        Engine engine;
        GameState state;
        engine.st = &state;
        std::uint64_t key = engine._tt_key(&state);
        engine._store(key, 6, WIN_SCORE - 5, -INFINITY_SCORE, INFINITY_SCORE, 5, 0);
        int d, v, f;
        std::vector<int> t;
        CHECK(engine.tt_lookup(key, d, v, f, t));
        CHECK_EQ(v, WIN_SCORE);
        CHECK_EQ(score_from_tt(v, 9), WIN_SCORE - 9);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1", "I9"});
        Engine engine(false, false, false, true, false, false, false);
        engine.use_lmp = true;
        engine.use_rfp = false;
        engine.st = &state;
        int n = engine._fill_actions(0, -1, 1, state.placements_left(),
                                     state.placed_this_turn);
        int quiet_cell = -1;
        int n_quiet = 0;
        for (int i = 0; i < n; ++i) {
            if (engine.move_kind[1][i] == KIND_QUIET) {
                quiet_cell = engine.move_buf[1][i];
                n_quiet += 1;
            }
        }
        CHECK_GE(quiet_cell, 0);
        CHECK_GT(n_quiet, LMP_QUIET_D1);
        engine.history[state.to_move][hist_slot(state.placed_this_turn)][quiet_cell] = 400;
        engine.deadline.store(now() + 30.0);
        engine.nodes = 0;
        (void)engine._negamax(1, -INFINITY_SCORE, INFINITY_SCORE,
                              state.max_this_turn, 0, 1, 0, 0);
        bool kept = false;
        for (int i = 0; i < engine.move_len[1]; ++i) {
            if (engine.move_buf[1][i] == quiet_cell) kept = true;
        }
        CHECK(kept);
        CHECK_GT(engine.lmp_dropped, 0);
    }

    {
        GameState state;
        play_names(state, {"E5"});
        play_names(state, {"A1", "I9"});
        Engine engine(false, false, true, true, false, false, false);
        engine.use_lmp = true;
        engine.use_rfp = false;
        engine.use_nmp = false;
        engine.st = &state;
        int n = engine._fill_actions(0, -1, 1, state.placements_left(),
                                     state.placed_this_turn);
        int quiet_cell = -1;
        for (int i = 0; i < n; ++i) {
            if (engine.move_kind[1][i] == KIND_QUIET) {
                quiet_cell = engine.move_buf[1][i];
                break;
            }
        }
        CHECK_GE(quiet_cell, 0);
        int row = hist_slot(state.placed_this_turn);
        engine.history[state.to_move][row][quiet_cell] = -(HIST_MAX / 4) - 1;
        engine.deadline.store(now() + 30.0);
        engine.lmp_dropped = 0;
        (void)engine._negamax(1, -INFINITY_SCORE, INFINITY_SCORE,
                              state.max_this_turn, 0, 1, 0, 0);
        bool kept = false;
        for (int i = 0; i < engine.move_len[1]; ++i) {
            if (engine.move_buf[1][i] == quiet_cell) kept = true;
        }
        CHECK(!kept);
        CHECK_GT(engine.lmp_dropped, 0);

        engine.killer0[1] = quiet_cell;
        engine.lmp_dropped = 0;
        (void)engine._negamax(1, -INFINITY_SCORE, INFINITY_SCORE,
                              state.max_this_turn, 0, 1, 0, 0);
        kept = false;
        for (int i = 0; i < engine.move_len[1]; ++i) {
            if (engine.move_buf[1][i] == quiet_cell) kept = true;
        }
        CHECK(kept);
    }

    {
        Engine engine;
        CHECK_EQ(engine._lmr_reduction(12, 40, KIND_EXPAND), 2);
        CHECK_EQ(engine._lmr_reduction(12, 40, KIND_HARRASS), 4);
        CHECK_EQ(engine._lmr_reduction(12, 40, KIND_QUIET), engine.lmr_quiet_cap);
        CHECK_EQ(engine._lmr_reduction(12, 40, KIND_MERGE), 3);
        CHECK_EQ(engine._lmr_reduction(12, 40, KIND_CRIT_MERGE), 0);
        CHECK_EQ(engine._lmr_reduction(12, 40, KIND_CRIT_BLOCK), 0);
        CHECK_EQ(engine._lmr_reduction(9, 40, KIND_EXPAND), 1);
        CHECK_EQ(engine._lmr_reduction(9, 40, KIND_HARRASS), 2);
    }

    {
        Engine engine;
        std::uint64_t k1 = 3;
        std::uint64_t k2 = 3 + static_cast<std::uint64_t>(TT_BUCKETS);
        std::uint64_t k3 = 3 + 2ull * TT_BUCKETS;
        engine._write_tt(k1, 8, 100, EXACT, std::vector<int>{1});
        engine._write_tt(k2, 7, 80, EXACT, std::vector<int>{2});
        engine._write_tt(k3, 1, 5, EXACT, std::vector<int>{4});
        int d, v, f;
        std::vector<int> t;
        CHECK(engine.tt_lookup(k1, d, v, f, t));
        CHECK_EQ(d, 8);
        CHECK(engine.tt_lookup(k2, d, v, f, t));
        CHECK_EQ(d, 7);
        CHECK(!engine.tt_lookup(k3, d, v, f, t));
    }

    {
        GameState won;
        force_side(won, WHITE);
        int a1 = name_to_index("A1");
        int i8 = name_to_index("I8");
        int i9 = name_to_index("I9");
        for (int c = 0; c < NUM_CELLS; ++c) {
            if (c == a1 || c == i8 || c == i9) continue;
            won.place(c);
        }
        force_side(won, BLACK);
        won.place(a1);
        Engine engine;
        engine.st = &won;
        force_side(won, WHITE);
        CHECK_EQ(engine._evaluate(0, 5), WIN_SCORE - 5);
        force_side(won, BLACK);
        CHECK_EQ(engine._evaluate(0, 5), 5 - WIN_SCORE);
    }

    {
        GameState state = random_position(17, 12);
        Engine left(true, true, true, true, true, true, false, 2);
        Engine right(true, true, true, true, true, true, false, 2);
        GameState a = state.clone();
        GameState b = state.clone();
        auto first = left.find_turn(a, 2.0);
        auto second = right.find_turn(b, 2.0);
        CHECK(first.first == second.first);
        CHECK_EQ(first.second.value, second.second.value);
    }

    {
        GameState state = random_position(19, 24);
        Engine engine(true, true, true, true, true, true, false, 32);
        engine.move_wall = 0.05;
        auto result = engine.find_turn(state, 30.0);
        CHECK_GE(static_cast<int>(result.first.size()), 1);
        CHECK_LT(result.second.elapsed, 3.0);
    }

    {
        GameState state = random_position(23, 16);
        Engine engine(true, true, true, true, true, true, false, 6);
        engine.tt_keep_deep = true;
        engine.lmp_by_placements = true;
        engine.swing_asymmetric = true;
        engine.history_gift_split = true;
        engine.use_iid = true;
        auto result = engine.find_turn(state, 0.4);
        CHECK_GE(static_cast<int>(result.first.size()), 1);
    }

    {
        GameState state;
        while (state.empty_count > 20) {
            int budget = state.max_this_turn;
            int n = 0;
            for (int c = 0; c < NUM_CELLS && n < budget && state.empty_count > 20; ++c) {
                if (state.colour[c] != EMPTY) continue;
                state.place(c);
                n += 1;
            }
            state.end_turn();
        }
        CHECK_GT(state.empty_count, EXACT_EMPTY_MAX);
        CHECK_EQ(state.placed_this_turn, 0);
        CHECK(!exact_solve_position(state));

        const int depth = 8;
        const int beta = -4000;
        int side = state.to_move;
        int placed = state.placed_this_turn;
        bool three = state.allows_three;
        std::uint64_t z = state.zobrist;

        Engine off(false, true, false, false, false, false, false);
        off.use_rfp = false;
        off.use_lmp = false;
        off.use_iid = false;
        off.use_nmp = false;
        off.deadline.store(now() + 30.0);
        GameState off_state = state.clone();
        off.st = &off_state;
        off.nodes = 0;
        (void)off._negamax(depth, -INFINITY_SCORE, beta, off_state.max_this_turn,
                           0, 1, 0, 0);

        Engine on(false, true, false, false, false, false, false);
        on.use_rfp = false;
        on.use_lmp = false;
        on.use_iid = false;
        on.use_nmp = true;
        on.deadline.store(now() + 30.0);
        on.st = &state;
        on.nodes = 0;
        on.nmp_cuts = 0;
        int value = on._negamax(depth, -INFINITY_SCORE, beta, state.max_this_turn,
                                0, 1, 0, 0);
        CHECK_GT(on.nmp_cuts, 0);
        CHECK_GE(value, beta);
        CHECK_LT(on.nodes * 3, off.nodes);
        CHECK_EQ(state.to_move, side);
        CHECK_EQ(state.placed_this_turn, placed);
        CHECK_EQ(state.allows_three, three);
        CHECK_EQ(state.zobrist, z);
    }

    {
        GameState state;
        while (state.empty_count > EXACT_EMPTY_MAX) {
            int budget = state.max_this_turn;
            int n = 0;
            for (int c = 0; c < NUM_CELLS && n < budget
                 && state.empty_count > EXACT_EMPTY_MAX; ++c) {
                if (state.colour[c] != EMPTY) continue;
                state.place(c);
                n += 1;
            }
            if (n == 0) break;
            state.end_turn();
        }
        CHECK_LE(state.empty_count, EXACT_EMPTY_MAX);
        CHECK_EQ(state.placed_this_turn, 0);
        Engine engine(false, false, false, false, false, false, false);
        engine.use_rfp = false;
        engine.use_lmp = false;
        engine.use_nmp = true;
        engine.st = &state;
        engine.deadline.store(now() + 0.2);
        engine.nmp_cuts = 0;
        try {
            (void)engine._negamax(6, -INFINITY_SCORE, -4000, state.max_this_turn,
                                  0, 1, 0, 0);
        } catch (const TimeoutException&) {
        }
        CHECK_EQ(engine.nmp_cuts, 0);
    }

    {
        Engine engine;
        CHECK(!engine.use_race);
        CHECK(!engine.use_unique);
        CHECK(!engine.use_forced_gift);
        GameState empty;
        CHECK_EQ(race_advantage(empty, 1), 0);

        GameState pockets = split_pockets();
        std::uint64_t empty_bb = FULL_MASK ^ (pockets.bb[0] | pockets.bb[1]);
        std::uint64_t white_region = pockets.largest_region(WHITE);
        std::uint64_t black_region = pockets.largest_region(BLACK);
        CHECK_EQ(popcnt(white_region), pockets.largest_reachable(WHITE));
        CHECK_EQ(popcnt(black_region), pockets.largest_reachable(BLACK));
        GameState copied = pockets.clone();
        CHECK_EQ(copied.largest_region(WHITE), white_region);
        CHECK_EQ(copied.largest_region(BLACK), black_region);
        int shared = popcnt(white_region & black_region & empty_bb);
        CHECK_LE(shared, RACE_SHARED_MAX);
        CHECK_GT(pockets.largest_reachable(WHITE), pockets.largest_reachable(BLACK));
        CHECK_GT(pockets.largest[WHITE], pockets.largest[BLACK]);
        int white_holes = popcnt(white_region & empty_bb) - shared;
        int black_holes = popcnt(black_region & empty_bb) - shared;
        CHECK_LT(white_holes, black_holes);

        settle_side(pockets, WHITE);
        int white_turn = race_advantage(pockets, pockets.placements_left());
        settle_side(pockets, BLACK);
        int black_turn = race_advantage(pockets, pockets.placements_left());
        CHECK_GT(white_turn, 0);
        CHECK_LT(black_turn, 0);

        settle_side(pockets, WHITE);
        Engine off = weights_engine(100, 0);
        Engine on = weights_engine(100, 0);
        on.use_race = true;
        off.st = &pockets;
        on.st = &pockets;
        int plain = off._evaluate(pockets.placements_left());
        int raced = on._evaluate(pockets.placements_left());
        CHECK_GT(raced, plain);
    }

    {
        auto build = [](bool block_second) {
            GameState state;
            for (const char* name : {"A1", "B1", "C1", "D1", "A3", "B3", "C3"}) {
                place_as(state, name_to_index(name), WHITE);
            }
            place_as(state, name_to_index("B2"), BLACK);
            if (block_second) place_as(state, name_to_index("C2"), BLACK);
            settle_side(state, WHITE);
            return state;
        };
        GameState one_lib = build(true);
        GameState two_lib = build(false);
        Engine engine = weights_engine(0, 0);
        engine.use_unique = true;
        engine.st = &one_lib;
        int one_score = engine._evaluate(one_lib.placements_left());
        engine.st = &two_lib;
        int two_score = engine._evaluate(two_lib.placements_left());
        CHECK_EQ(one_score, UNIQUE_WEIGHT);
        CHECK_EQ(two_score, 0);
        CHECK_GT(one_score, two_score);

        Engine saturated = weights_engine(100, 0);
        saturated.use_unique = true;
        saturated.st = &one_lib;
        int with_term = saturated._evaluate(one_lib.placements_left());
        saturated.use_unique = false;
        int without_term = saturated._evaluate(one_lib.placements_left());
        int softened = UNIQUE_WEIGHT * 100 / (100 + UNIQUE_WEIGHT);
        CHECK_EQ(with_term - without_term, softened);
    }

    {
        // B2 is the only empty. A2 is a black singleton whose other neighbours
        // are white, so B2 is a +2 black merge. C2-D2 grow the main black
        // group out to the right; the white bridge keeps A2 sealed and still
        // touches B2. H4 is a white stone buried in black: leaving it empty
        // is a cell that does not touch white, so the gift stops being forced.
        const int gap = name_to_index("B2");
        const int island = name_to_index("A2");
        const int escape_cell = name_to_index("H4");
        int owner[NUM_CELLS];
        for (int c = 0; c < NUM_CELLS; ++c) owner[c] = -2;
        owner[gap] = -1;
        owner[island] = BLACK;
        const char* bridge[] = {
            "A1", "B1", "C1", "D1", "E1", "E2", "F2", "F3",
            "E4", "D4", "C4", "C3", "B3", "A3",
        };
        for (const char* name : bridge) owner[name_to_index(name)] = WHITE;
        int black_seed = name_to_index("C2");
        owner[black_seed] = BLACK;
        owner[name_to_index("D2")] = BLACK;
        int queued[NUM_CELLS];
        int qh = 0;
        int qt = 0;
        queued[qt++] = black_seed;
        queued[qt++] = name_to_index("D2");
        int black_n = 2;
        while (qh < qt && black_n < 28) {
            int cell = queued[qh++];
            int ncount = NEIGHBOUR_COUNT[cell];
            for (int i = 0; i < ncount; ++i) {
                int nb = NEIGHBOURS[cell][i];
                if (owner[nb] != -2) continue;
                owner[nb] = BLACK;
                black_n += 1;
                queued[qt++] = nb;
                if (black_n >= 28) break;
            }
        }
        for (int c = 0; c < NUM_CELLS; ++c) {
            if (owner[c] == -2) owner[c] = WHITE;
        }
        owner[escape_cell] = WHITE;
        auto paint = [&](bool keep_escape) {
            GameState state;
            for (int c = 0; c < NUM_CELLS; ++c) {
                if (c == gap) continue;
                if (c == escape_cell && !keep_escape) continue;
                int col = owner[c];
                if (col < 0) col = BLACK;
                place_as(state, c, col);
            }
            settle_side(state, WHITE);
            return state;
        };
        GameState forced = paint(true);
        GameState escape = paint(false);
        CHECK(open_forced_gift(forced));
        CHECK(!open_forced_gift(escape));
        CHECK_EQ(forced.placed_this_turn, 0);
        CHECK_EQ(forced.largest[WHITE], escape.largest[WHITE]);
        CHECK_EQ(forced.largest[BLACK], escape.largest[BLACK]);

        Engine off = weights_engine(100, 80);
        Engine on = weights_engine(100, 80);
        on.use_forced_gift = true;
        off.st = &forced;
        on.st = &forced;
        int forced_off = off._evaluate(forced.placements_left());
        int forced_on = on._evaluate(forced.placements_left());
        CHECK_LT(forced_on, forced_off);
        off.st = &escape;
        on.st = &escape;
        int escape_off = off._evaluate(escape.placements_left());
        int escape_on = on._evaluate(escape.placements_left());
        CHECK_EQ(escape_on, escape_off);
        CHECK_LT(forced_on, escape_on);
    }
}
