// Correctness oracle: compare the optimised search with a plain reference
// search on small positions (added during repository preparation, see
// docs/PROVENANCE.md).
//
// The reference uses the engine's own rules (GameState::place / end_turn /
// terminal_score), so it compares *search*, not rules. It has no alpha-beta,
// no TT, no symmetry, no move ordering and no pruning. The only shortcut is
// that the stones of one turn are enumerated as a set (increasing cell index)
// rather than in every order. That is sound only if the position after a turn
// depends on the set of stones, not on their order. check_order_independence()
// below verifies that assumption on random positions.

#include "check.hpp"
#include "position.hpp"
#include "search.hpp"
#include "state.hpp"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <random>
#include <set>
#include <vector>

using namespace catchup;

namespace {

double ref_now() {
    return std::chrono::duration<double>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
}

int sign(int v) { return (v > 0) - (v < 0); }

long g_ref_nodes = 0;

// Game-theoretic value (+1 win, 0 draw, -1 loss) for s.to_move. Remaining
// stones of the current turn may go on any empty cell with index >= start.
int ref_value(GameState& s, int start) {
    ++g_ref_nodes;
    if (s.empty_count == 0) return sign(s.terminal_score(s.to_move));
    int best = -2;
    if (s.placed_this_turn >= 1) {
        s.end_turn();
        int v = -ref_value(s, 0);
        s.unend_turn();
        best = std::max(best, v);
    }
    if (s.placements_left() > 0) {
        for (int c = start; c < NUM_CELLS; ++c) {
            if (s.colour[c]) continue;
            s.place(c);
            int v = ref_value(s, c + 1);
            s.unplace();
            best = std::max(best, v);
        }
    }
    return best;
}

// -1 / 0 / +1 if the engine value is a proved result, 99 if it is a heuristic.
int engine_outcome(int v) {
    if (v > MATE_THRESHOLD) return 1;
    if (v < -MATE_THRESHOLD) return -1;
    if (v == 0) return 0;
    return 99;
}

struct Game {
    std::vector<std::vector<int>> turns;  // completed turns
    std::vector<int> partial;             // stones of the unfinished turn
};

// Random game with `empties` empty cells left, optionally one stone into a turn.
Game random_game(std::mt19937& rng, int empties, bool mid_turn) {
    for (;;) {
        Game g;
        GameState s;
        bool ok = true;
        while (s.empty_count > empties) {
            int k = std::uniform_int_distribution<int>(1, s.max_this_turn)(rng);
            k = std::min(k, s.empty_count - empties);
            std::vector<int> empty;
            for (int c = 0; c < NUM_CELLS; ++c) if (!s.colour[c]) empty.push_back(c);
            std::shuffle(empty.begin(), empty.end(), rng);
            std::vector<int> turn(empty.begin(), empty.begin() + k);
            s.play_turn(turn);
            g.turns.push_back(turn);
        }
        if (mid_turn) {
            if (s.max_this_turn < 2) ok = false;
            else {
                std::vector<int> empty;
                for (int c = 0; c < NUM_CELLS; ++c) if (!s.colour[c]) empty.push_back(c);
                g.partial.push_back(empty[rng() % empty.size()]);
            }
        }
        if (ok) return g;
    }
}

GameState replay(const Game& g, int sym) {
    GameState s;
    for (const auto& t : g.turns) {
        std::vector<int> m;
        for (int c : t) m.push_back(SYM.perm[sym][c]);
        s.play_turn(m);
    }
    for (int c : g.partial) s.place(SYM.perm[sym][c]);
    return s;
}

struct OracleStats {
    int positions = 0;
    int value_mismatch = 0;
    int unsolved = 0;
    int bad_move = 0;
    int ref_asymmetric = 0;
    long ref_nodes = 0;
    double seconds = 0;
};

// One engine per family is reused for all 12 mirror images, so TT and
// endgame-table entries written for one image can be hit by another: that is
// exactly where a symmetry-unsound key would show up.
// full: also recompute the reference for every mirror image (rules symmetry
// cross-check); otherwise the reference value of image 0 is reused.
OracleStats run_oracle(int families, int empties, bool mid_turn, std::uint32_t seed,
                       bool verbose, bool full) {
    OracleStats st;
    std::mt19937 rng(seed);
    double t0 = ref_now();
    for (int f = 0; f < families; ++f) {
        Game g = random_game(rng, empties, mid_turn);
        Engine engine(true, true, true, true, true, true, false);
        int ref0 = 0;
        for (int sym = 0; sym < N_SYMMETRIES; ++sym) {
            GameState s = replay(g, sym);
            int ref = ref0;
            if (sym == 0 || full) {
                GameState r = s.clone();
                long before = g_ref_nodes;
                ref = ref_value(r, 0);
                st.ref_nodes += g_ref_nodes - before;
                if (sym == 0) ref0 = ref;
                else if (ref != ref0) ++st.ref_asymmetric;
            }

            int value;
            std::vector<int> turn;
            if (!mid_turn) {
                GameState copy = s.clone();
                auto res = engine.find_turn(copy, 60.0);
                value = res.second.value;
                turn = res.first;
            } else {
                GameState copy = s.clone();
                engine.st = &copy;
                engine.deadline.store(ref_now() + 60.0);
                engine.nodes = 0;
                engine.ply_prev[0] = -1;
                value = engine._negamax(copy.empty_count + 4, -INFINITY_SCORE,
                                        INFINITY_SCORE, copy.placements_left(),
                                        0, 0, 0, 0);
                engine.st = nullptr;
            }
            ++st.positions;
            int out = engine_outcome(value);
            if (out == 99) ++st.unsolved;
            else if (out != ref) ++st.value_mismatch;
            if ((out == 99 || out != ref) && verbose) {
                std::printf("  oracle mismatch: family %d sym %d %s: engine %d, reference %+d\n",
                            f, sym, mid_turn ? "mid-turn" : "turn-start", value, ref);
            }
            if (!mid_turn) {
                GameState after = s.clone();
                after.play_turn(turn);
                int got = after.empty_count == 0
                              ? sign(after.terminal_score(s.to_move))
                              : -ref_value(after, 0);
                if (got != ref) {
                    ++st.bad_move;
                    if (verbose) {
                        std::printf("  oracle: family %d sym %d chosen turn scores %+d, best %+d\n",
                                    f, sym, got, ref);
                    }
                }
            }
        }
    }
    st.seconds = ref_now() - t0;
    return st;
}

void check_order_independence() {
    std::mt19937 rng(20261118);
    int compared = 0;
    for (int trial = 0; trial < 200; ++trial) {
        GameState base = random_position(5000 + trial,
                                         10 + static_cast<int>(rng() % 40));
        if (base.max_this_turn < 2 || base.empty_count < 3) continue;
        std::vector<int> empty;
        for (int c = 0; c < NUM_CELLS; ++c) if (!base.colour[c]) empty.push_back(c);
        std::shuffle(empty.begin(), empty.end(), rng);
        std::vector<int> cells(empty.begin(), empty.begin() + base.max_this_turn);
        std::sort(cells.begin(), cells.end());
        GameState first = base.clone();
        first.play_turn(cells);
        do {
            GameState other = base.clone();
            other.play_turn(cells);
            ++compared;
            CHECK_EQ(other.zobrist, first.zobrist);
            CHECK_EQ(other.to_move, first.to_move);
            CHECK_EQ(other.max_this_turn, first.max_this_turn);
            CHECK_EQ(other.score_at_turn_start, first.score_at_turn_start);
            CHECK_EQ(other.allows_three, first.allows_three);
            CHECK_EQ(other.score_increased[0], first.score_increased[0]);
            CHECK_EQ(other.score_increased[1], first.score_increased[1]);
            CHECK_EQ(other.largest[0], first.largest[0]);
            CHECK_EQ(other.largest[1], first.largest[1]);
            CHECK_EQ(other.second[0], first.second[0]);
            CHECK_EQ(other.second[1], first.second[1]);
        } while (std::next_permutation(cells.begin(), cells.end()));
    }
    CHECK_GT(compared, 100);
}

// Regression for the mid-turn symmetry collision: mirror-image positions whose
// remaining legal placements (index >= min_index) are not images of each other
// must not share a TT key. Turn-start keys of mirror images must still match.
void check_mirror_keys() {
    int pairs = 0;
    int distinct_sets = 0;
    for (int g = 1; g < N_SYMMETRIES; ++g) {
        for (int b = 0; b < NUM_CELLS; ++b) {
            if (SYM.perm[g][b] != b) continue;
            for (int w = 0; w < NUM_CELLS; ++w) {
                int sw = SYM.perm[g][w];
                if (w == b || sw == w || sw == b) continue;
                GameState P; P.play_turn({w});
                GameState Q; Q.play_turn({sw});
                CHECK_EQ(get_canonical_key(P, 0), get_canonical_key(Q, 0));
                P.place(b);
                Q.place(b);
                int m = b + 1;
                std::set<int> gp, q;
                for (int c = m; c < NUM_CELLS; ++c) {
                    if (!P.colour[c]) gp.insert(SYM.perm[g][c]);
                    if (!Q.colour[c]) q.insert(c);
                }
                ++pairs;
                if (gp != q) {
                    ++distinct_sets;
                    CHECK_NE(get_canonical_key(P, m), get_canonical_key(Q, m));
                }
            }
        }
    }
    CHECK_GT(distinct_sets, 0);
    std::printf("  mirror-key regression: %d mirror pairs, %d with non-equivalent move sets\n",
                pairs, distinct_sets);
}

}  // namespace

void run_reference_tests() {
    check_order_independence();
    check_mirror_keys();

    // Default: a quick subset (seconds). CATCHUP_ORACLE_FULL=1 runs the full
    // set used for the before/after comparison in docs/PROVENANCE.md (minutes),
    // including the symmetry cross-check of the reference itself.
    const char* env = std::getenv("CATCHUP_ORACLE_FULL");
    bool full = env && env[0] == '1';
    struct Case { int families; int empties; bool mid; std::uint32_t seed; };
    const Case quick[] = {
        {12, 8, false, 1},
        {12, 8, true, 2},
    };
    const Case all[] = {
        {12, 8, false, 1},
        {12, 8, true, 2},
        {6, 9, false, 3},
        {6, 9, true, 4},
    };
    const Case* begin = full ? all : quick;
    const Case* end = full ? all + 4 : quick + 2;
    for (const Case* it = begin; it != end; ++it) {
        const Case& c = *it;
        OracleStats st = run_oracle(c.families, c.empties, c.mid, c.seed, true, full);
        std::printf("  oracle %-10s empties=%d: %d positions, value mismatches %d, "
                    "unsolved %d, suboptimal turns %d, reference asymmetric %d, "
                    "%ld reference nodes, %.1fs\n",
                    c.mid ? "mid-turn" : "turn-start", c.empties, st.positions,
                    st.value_mismatch, st.unsolved, st.bad_move, st.ref_asymmetric,
                    st.ref_nodes, st.seconds);
        CHECK_EQ(st.ref_asymmetric, 0);
        CHECK_EQ(st.value_mismatch, 0);
        CHECK_EQ(st.unsolved, 0);
        CHECK_EQ(st.bad_move, 0);
    }
}
