#include "search.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>

namespace catchup {
namespace {

double now() {
    using clock = std::chrono::steady_clock;
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

int LMR_TABLE[LMR_TABLE_N][LMR_TABLE_N];

struct LmrTableInit {
    LmrTableInit() {
        for (int d = 0; d < LMR_TABLE_N; ++d) {
            for (int s = 0; s < LMR_TABLE_N; ++s) {
                if (d < 1 || s < 1) {
                    LMR_TABLE[d][s] = 0;
                    continue;
                }
                double r = std::log(static_cast<double>(d))
                           * std::log(static_cast<double>(s)) / 1.8;
                int red = static_cast<int>(r);
                LMR_TABLE[d][s] = red < 0 ? 0 : red;
            }
        }
    }
} lmr_table_init;

}  // namespace

std::string SearchStats::describe() const {
    std::ostringstream oss;
    oss << "depth " << depth << "  value ";
    if (value >= 0) oss << "+";
    oss << value << "  " << nodes << " nodes in ";
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", elapsed);
    oss << buf << "s (";
    std::snprintf(buf, sizeof(buf), "%.0f", nps());
    oss << buf << " n/s)  turn";
    for (int c : turn) {
        oss << " " << CELL_NAMES[c];
    }
    oss << "  tt " << tt_hits << "/" << tt_probes << " cut " << tt_cutoffs
        << "  1st " << first_fail_high
        << "  lmr " << lmr_researched << "/" << lmr_reduced
        << "  lmp -" << lmp_dropped
        << "  nmp " << nmp_cuts
        << "  kind";
    static const char* knames[] = {"M", "B", "E", "H", "Q"};
    for (int k = 0; k < N_BUCKETS; ++k) {
        oss << " " << knames[k] << nodes_kind[k];
    }
    oss << " ET" << nodes_end_turn;
    return oss.str();
}

void Clock::note_own_stones(int stones) {
    cycle_undo.push_back({avg_cycle, cycle_samples, pending_own});
    pending_own = stones;
}

void Clock::note_reply_stones(int stones) {
    cycle_undo.push_back({avg_cycle, cycle_samples, pending_own});
    if (pending_own < 0) return;
    double cycle = static_cast<double>(pending_own + stones);
    if (cycle < 1.0) cycle = 1.0;
    avg_cycle = (avg_cycle * cycle_samples + cycle) / (cycle_samples + 1);
    cycle_samples += 1;
    pending_own = -1;
}

void Clock::undo_note() {
    if (cycle_undo.empty()) return;
    CycleSnap snap = cycle_undo.back();
    cycle_undo.pop_back();
    avg_cycle = snap.avg_cycle;
    cycle_samples = snap.cycle_samples;
    pending_own = snap.pending_own;
}

double Clock::allocate(const GameState& state) const {
    double wall = total - used;
    if (wall <= 0.05) return 0.05;
    // Below one second, take a fixed sliver and let the search skip aspiration.
    if (wall < 1.0) return std::min(0.15, wall);

    int empty = state.empty_count;
    double cycle = avg_cycle > 0.5 ? avg_cycle : 3.6;
    int own_turns_left = std::max(1, static_cast<int>(empty / cycle));
    bool exact = exact_solve_position(state);
    // Last two own turns may spend the reserve. The final turn keeps 1s.
    bool late = own_turns_left <= 2;

    if (own_turns_left <= 1) return std::max(0.05, wall - 1.0);

    // Exact endgames are the turns the reserve exists for. Spend wall time,
    // which includes the reserve, instead of nibbling it at 0.05 s.
    if (exact && empty <= ENDGAME_EMPTY) {
        double budget = wall / own_turns_left * 1.4;
        double cap = own_turns_left <= 3
                         ? std::max(0.05, 0.90 * wall - 0.5)
                         : 0.40 * wall;
        double slice = budget < cap ? budget : cap;
        double ceiling = late ? (wall - 1.0) : wall;
        if (slice > ceiling) slice = ceiling;
        return std::max(0.05, slice);
    }

    double spendable = late ? wall : remaining();
    if (spendable <= 0.05) return std::min(0.15, wall);

    double factor = 1.0;
    if (state.max_this_turn == 3) factor *= 1.7;

    int vol = merge_volatility(state);
    // Quiet turns stay on the 0.75 side of an even split. The tail is the
    // volatility and gift multipliers, not a larger quiet budget.
    if (vol <= 1) factor *= 0.75;
    else if (vol <= 3) factor *= 1.15;
    else factor *= 1.45;

    if (exact) factor *= 1.4;
    else if (empty > 50) factor *= 1.15;
    else if (empty > 14 && empty <= 40) factor *= 1.25;
    else if (empty > 8 && empty <= 14) factor *= 1.1;

    double budget = spendable / own_turns_left * factor;
    bool critical = state.max_this_turn == 3 || vol >= 4;
    // 0.28 only binds once few turns remain. Raise that late critical cap.
    double cap_frac = 0.22;
    if (critical) cap_frac = own_turns_left <= 6 ? 0.40 : 0.28;
    double cap = spendable * cap_frac;
    if (own_turns_left <= 3) {
        cap = std::max(0.05, 0.90 * spendable - 1.0);
    }
    double ceiling = late ? (wall - 1.0) : spendable;
    double slice = std::min({budget, cap, ceiling});
    return std::max(0.2, slice);
}

int pack_cells(int c0, int c1, int c2, int n) {
    return n | (c0 << 2) | (c1 << 8) | (c2 << 14);
}

std::vector<int> unpack_cells(int packed) {
    int n = packed & 3;
    if (n == 0) return {};
    int c0 = (packed >> 2) & 63;
    if (n == 1) return {c0};
    int c1 = (packed >> 8) & 63;
    if (n == 2) return {c0, c1};
    return {c0, c1, (packed >> 14) & 63};
}

// Map the cells of a packed PV between a position's own frame and the
// canonical frame used by its TT key (see get_canonical_key).
int pack_to_canonical(int packed, int sym) {
    if (sym == 0) return packed;
    int n = packed & 3;
    int out = n;
    for (int i = 0; i < n; ++i) {
        int c = (packed >> (2 + 6 * i)) & 63;
        out |= SYM.perm[sym][c] << (2 + 6 * i);
    }
    return out;
}

int pack_from_canonical(int packed, int sym) {
    if (sym == 0) return packed;
    int n = packed & 3;
    int out = n;
    for (int i = 0; i < n; ++i) {
        int c = (packed >> (2 + 6 * i)) & 63;
        out |= sym_inverse_cell(sym, c) << (2 + 6 * i);
    }
    return out;
}

int hint_cell(int packed, int off) {
    if (off >= (packed & 3)) return -1;
    return (packed >> (2 + 6 * off)) & 63;
}

int kind_from_masks(std::uint64_t bit, std::uint64_t merge, std::uint64_t expand,
                    std::uint64_t block, std::uint64_t harass) {
    if (merge & bit) return KIND_MERGE;
    if (block & bit) return KIND_BLOCK;
    if (expand & bit) return KIND_EXPAND;
    if (harass & bit) return KIND_HARRASS;
    return KIND_QUIET;
}

int extract_bits(std::uint64_t mask, int* buf) {
    int n = 0;
    while (mask) {
        std::uint64_t lsb = mask & -mask;
        buf[n++] = ctz(lsb);
        mask ^= lsb;
    }
    return n;
}

int classify_cell(int cell, const std::uint8_t* adj_me, const std::uint8_t* adj_opp,
                  std::uint64_t lib_me, std::uint64_t lib_opp, bool ahead) {
    int am = adj_me[cell];
    int ao = adj_opp[cell];
    std::uint64_t bit = CELL_BIT[cell];
    if (am == 0 && ao == 0) return KIND_QUIET;
    if (am) {
        if ((lib_me & bit) || am >= 2) return ahead ? KIND_EXPAND : KIND_MERGE;
        return KIND_EXPAND;
    }
    if (lib_opp & bit) return KIND_BLOCK;
    return KIND_HARRASS;
}

int own_group_count(const GameState& s, int cell, int player) {
    int stone = STONE[player];
    int roots[6];
    int nroots = 0;
    int ncount = NEIGHBOUR_COUNT[cell];
    for (int i = 0; i < ncount; ++i) {
        int n = NEIGHBOURS[cell][i];
        if (s.colour[n] != stone) continue;
        int r = s.find(n);
        bool seen = false;
        for (int k = 0; k < nroots; ++k) {
            if (roots[k] == r) {
                seen = true;
                break;
            }
        }
        if (seen) continue;
        roots[nroots++] = r;
    }
    return nroots;
}

int joined_group_size(const GameState& s, int cell, int player) {
    int stone = STONE[player];
    int roots[6];
    int nroots = 0;
    int total = 0;
    int ncount = NEIGHBOUR_COUNT[cell];
    for (int i = 0; i < ncount; ++i) {
        int n = NEIGHBOURS[cell][i];
        if (s.colour[n] != stone) continue;
        int r = s.find(n);
        bool seen = false;
        for (int k = 0; k < nroots; ++k) {
            if (roots[k] == r) {
                seen = true;
                break;
            }
        }
        if (seen) continue;
        roots[nroots++] = r;
        total += s.size[r];
    }
    if (nroots < 2) return 0;
    return total;
}

int placement_gain(const GameState& s, int cell, int player) {
    int stone = STONE[player];
    int largest = s.largest[player];
    int roots[6];
    int nroots = 0;
    int total = 0;
    int ncount = NEIGHBOUR_COUNT[cell];
    for (int i = 0; i < ncount; ++i) {
        int n = NEIGHBOURS[cell][i];
        if (s.colour[n] != stone) continue;
        int r = s.find(n);
        bool seen = false;
        for (int k = 0; k < nroots; ++k) {
            if (roots[k] == r) {
                seen = true;
                break;
            }
        }
        if (seen) continue;
        roots[nroots++] = r;
        total += s.size[r];
    }
    if (nroots == 0) return 0;
    int neu = total + 1;
    return neu > largest ? neu - largest : 0;
}

static int denial_swing(int og, int ed, bool asymmetric) {
    if (asymmetric) return 2 * og + 3 * ed;
    return og + (ed >= CRIT_GAIN ? 2 * ed : ed);
}

static int soft_saturate(int value, int cap) {
    if (cap <= 0 || value == 0) return value;
    int mag = value < 0 ? -value : value;
    int softened = static_cast<int>(
        (static_cast<long long>(mag) * cap) / (cap + mag));
    return value < 0 ? -softened : softened;
}

static BridgeInfo compute_own_bridges(const GameState& s, int player, int max_k) {
    BridgeInfo info{0, 0, 0};
    if (max_k < 1 || s.second[player] < 2) return info;

    std::uint64_t empty = FULL_MASK ^ (s.bb[0] | s.bb[1]);
    std::uint64_t libL = s.largest_lib_mask(player) & empty;
    if (!libL) return info;

    int lg = s.largest[player];
    int skipped_largest = 0;
    std::uint64_t stones = s.bb[player];
    while (stones) {
        std::uint64_t bit = stones & -stones;
        int cell = ctz(bit);
        stones ^= bit;
        if (s.parent[cell] != cell) continue;
        int sz = s.size[cell];
        if (sz < 2) continue;
        if (sz == lg) {
            if (skipped_largest == 0) {
                skipped_largest = 1;
                continue;
            }
        }
        std::uint64_t libS = s.lib_root[cell] & empty;
        if (!libS) continue;

        auto note = [&](std::uint64_t path, int k) {
            if (k > max_k || !path) return;
            int gain = sz + k;
            info.cells |= path;
            info.starters |= CELL_BIT[ctz(path)];
            if (gain > info.gain) info.gain = gain;
        };

        if (max_k >= 1) note(libL & libS, 1);

        if (max_k >= 2) {
            std::uint64_t a = libS;
            while (a) {
                std::uint64_t ab = a & -a;
                a ^= ab;
                std::uint64_t hit = NEIGHBOUR_MASK[ctz(ab)] & libL;
                while (hit) {
                    std::uint64_t hb = hit & -hit;
                    hit ^= hb;
                    note(ab | hb, 2);
                }
            }
        }

        if (max_k >= 3) {
            std::uint64_t a = libS;
            while (a) {
                std::uint64_t ab = a & -a;
                a ^= ab;
                std::uint64_t mids = NEIGHBOUR_MASK[ctz(ab)] & empty & ~libL & ~libS;
                while (mids) {
                    std::uint64_t mb = mids & -mids;
                    mids ^= mb;
                    std::uint64_t ends = NEIGHBOUR_MASK[ctz(mb)] & libL;
                    while (ends) {
                        std::uint64_t eb = ends & -ends;
                        ends ^= eb;
                        note(ab | mb | eb, 3);
                    }
                }
            }
        }
    }
    return info;
}

BridgeInfo own_bridges(const GameState& s, int player, int max_k) {
    if (player >= 0 && player < 2 && s.cache_bridge_valid[player]
        && s.cache_bridge_k[player] == max_k) {
        return s.cache_bridge[player];
    }
    BridgeInfo info = compute_own_bridges(s, player, max_k);
    if (player >= 0 && player < 2) {
        s.cache_bridge[player] = info;
        s.cache_bridge_k[player] = max_k;
        s.cache_bridge_valid[player] = true;
    }
    return info;
}

int merge_volatility(const GameState& s) {
    int me = s.to_move;
    int opp = 1 - me;
    std::uint64_t empty_bb = FULL_MASK ^ (s.bb[0] | s.bb[1]);
    std::uint64_t cand = (s.touch[me] | s.touch[opp]) & empty_bb;
    int best = 0;
    int n_big = 0;
    while (cand) {
        std::uint64_t bit = cand & -cand;
        int x = ctz(bit);
        cand ^= bit;
        int og = placement_gain(s, x, me);
        int ed = placement_gain(s, x, opp);
        int swing = og > ed ? og : ed;
        if (swing > best) best = swing;
        if (swing >= 2) n_big += 1;
    }
    if (n_big >= 2) best += best ? best : 2;
    return best;
}

static int turns_to_place(int need, int first_budget, int later_budget) {
    if (need <= 0) return 0;
    if (later_budget < 1) later_budget = 1;
    if (first_budget <= 0) return (need + later_budget - 1) / later_budget;
    if (need <= first_budget) return 1;
    need -= first_budget;
    return 1 + (need + later_budget - 1) / later_budget;
}

int race_advantage(const GameState& s, int placements_left) {
    int empty = s.empty_count;
    if (empty <= 0 || empty > RACE_EMPTY_MAX) return 0;
    int me = s.to_move;
    int opp = 1 - me;
    std::uint64_t empty_bb = FULL_MASK ^ (s.bb[0] | s.bb[1]);
    std::uint64_t my_mask = s.largest_region(me);
    std::uint64_t opp_mask = s.largest_region(opp);
    int shared = popcnt(my_mask & opp_mask & empty_bb);
    if (shared > RACE_SHARED_MAX) return 0;
    int my_excl = popcnt(my_mask & empty_bb) - shared;
    int opp_excl = popcnt(opp_mask & empty_bb) - shared;
    if (my_excl == 0 && opp_excl == 0) return 0;

    int mine = s.largest[me];
    int theirs = s.largest[opp];
    // Filling exclusive holes grows the largest group. A side that is already
    // ahead gifts a 3-stone reply; a side that is behind and still filling
    // gifts us a 3-stone turn after theirs.
    int my_later = (theirs > mine && opp_excl > 0) ? 3 : 2;
    int opp_first = (mine >= theirs && my_excl > 0) ? 3 : 2;
    int my_turns = turns_to_place(my_excl, placements_left, my_later);
    int opp_turns = turns_to_place(opp_excl, opp_first, 2);
    if (my_turns == opp_turns) return 0;

    int my_lr = s.largest_reachable(me);
    int opp_lr = s.largest_reachable(opp);
    int gap = opp_turns - my_turns;
    if (my_lr > opp_lr && gap > 0) return RACE_WEIGHT;
    if (my_lr > opp_lr && gap < 0) return -RACE_WEIGHT / 2;
    if (opp_lr > my_lr && gap < 0) return -RACE_WEIGHT;
    if (opp_lr > my_lr && gap > 0) return RACE_WEIGHT / 2;
    return 0;
}

static int clean_unique_connectors(const GameState& s, int player) {
    if (s.nstones[player] == 0) return 0;
    int opp = 1 - player;
    std::uint64_t empty_bb = FULL_MASK ^ (s.bb[0] | s.bb[1]);
    std::uint64_t lib_l = s.largest_lib_mask(player) & empty_bb;
    if (!lib_l) return 0;
    int largest = s.largest[player];
    int count = 0;
    std::uint64_t stones = s.bb[player];
    while (stones) {
        std::uint64_t bit = stones & -stones;
        int cell = ctz(bit);
        stones ^= bit;
        if (s.parent[cell] != cell || s.size[cell] == largest || s.size[cell] <= 0) {
            continue;
        }
        std::uint64_t shared = s.lib_root[cell] & lib_l;
        if (popcnt(shared) != 1) continue;
        int c = ctz(shared);
        if (placement_gain(s, c, player) > placement_gain(s, c, opp)) count += 1;
    }
    return count;
}

static int unique_connector_term(const GameState& s, int w_largest) {
    int diff = clean_unique_connectors(s, s.to_move)
               - clean_unique_connectors(s, 1 - s.to_move);
    if (diff > UNIQUE_CAP) diff = UNIQUE_CAP;
    if (diff < -UNIQUE_CAP) diff = -UNIQUE_CAP;
    int term = UNIQUE_WEIGHT * diff;
    if (w_largest > 0) term = soft_saturate(term, w_largest);
    return term;
}

static bool forced_to_gift(const GameState& s) {
    if (s.placed_this_turn != 0 || s.empty_count <= 0) return false;
    int me = s.to_move;
    std::uint64_t empty_bb = FULL_MASK ^ (s.bb[0] | s.bb[1]);
    if (empty_bb & ~s.touch[me]) return false;
    int min_gain = NUM_CELLS;
    std::uint64_t cand = empty_bb;
    while (cand) {
        std::uint64_t bit = cand & -cand;
        int cell = ctz(bit);
        cand ^= bit;
        int gain = placement_gain(s, cell, me);
        if (gain < min_gain) min_gain = gain;
        if (min_gain <= 0) return false;
    }
    return s.largest[me] + min_gain >= s.largest[1 - me];
}

int best_placement_gain(const GameState& s, int player) {
    std::uint64_t empty_bb = FULL_MASK ^ (s.bb[0] | s.bb[1]);
    std::uint64_t cand = s.touch[player] & empty_bb;
    int best = 0;
    while (cand) {
        std::uint64_t bit = cand & -cand;
        int x = ctz(bit);
        cand ^= bit;
        int g = placement_gain(s, x, player);
        if (g > best) best = g;
    }
    return best;
}

bool exact_solve_position(const GameState& s) {
    int empty = s.empty_count;
    if (empty <= ENDGAME_EMPTY) return true;
    if (empty > EXACT_EMPTY_MAX) return false;
    int me = s.to_move;
    int opp = 1 - me;
    std::uint64_t empty_bb = FULL_MASK ^ (s.bb[0] | s.bb[1]);
    std::uint64_t tactical = (s.join[me] | s.join[opp]
                              | s.largest_lib_mask(me) | s.largest_lib_mask(opp))
                             & empty_bb;
    return popcnt(tactical) <= EXACT_TACTICAL_MAX;
}

int group_nth(const GameState& s, int player, int n) {
    if (n <= 0) return 0;
    const int* hist = s.size_n[player];
    int seen = 0;
    for (int sz = NUM_CELLS; sz > 0; --sz) {
        int count = hist[sz];
        while (count-- > 0) {
            if (++seen == n) return sz;
        }
    }
    return 0;
}

void insertion_sort_rank(int* buf, int start, int stop) {
    for (int i = start + 1; i < stop; ++i) {
        int cell = buf[i];
        int r = STATIC_RANK[cell];
        int j = i;
        while (j > start && STATIC_RANK[buf[j - 1]] > r) {
            buf[j] = buf[j - 1];
            --j;
        }
        buf[j] = cell;
    }
}

void insertion_sort_history(int* buf, int start, int stop, const int* hist) {
    for (int i = start + 1; i < stop; ++i) {
        int cell = buf[i];
        int score = hist[cell];
        int j = i;
        while (j > start && hist[buf[j - 1]] < score) {
            buf[j] = buf[j - 1];
            --j;
        }
        buf[j] = cell;
    }
}

void insertion_sort_swing(int* buf, int start, int stop, const int* swing,
                          const int* hist, bool use_hist) {
    for (int i = start + 1; i < stop; ++i) {
        int cell = buf[i];
        int sw = swing[cell];
        int h = use_hist ? hist[cell] : 0;
        int rank = STATIC_RANK[cell];
        int j = i;
        while (j > start) {
            int prev = buf[j - 1];
            int psw = swing[prev];
            int ph = use_hist ? hist[prev] : 0;
            int prank = STATIC_RANK[prev];
            bool better = (sw != psw) ? (sw > psw)
                          : (h != ph) ? (h > ph)
                          : (rank < prank);
            if (!better) break;
            buf[j] = prev;
            --j;
        }
        buf[j] = cell;
    }
}

void Engine::init_tt() {
    tt_d.assign(tt_buckets, TTSlot{});
    tt_a.assign(tt_buckets, TTSlot{});
    tt_locks.assign(tt_buckets, TTLock{});
    tt_generation = 1;
}

void Engine::tt_lock(int index) {
    while (tt_locks[index].held.test_and_set(std::memory_order_acquire)) {}
}

void Engine::tt_unlock(int index) {
    tt_locks[index].held.clear(std::memory_order_release);
}

std::uint64_t Engine::tt_chk(std::uint64_t key) const {
    return key >> tt_bits;
}

int Engine::tt_index(std::uint64_t key) const {
    return static_cast<int>(key & static_cast<std::uint64_t>(tt_mask));
}

bool Engine::tt_slot_live(const TTSlot& slot) const {
    return slot.depth >= 0 && slot.gen == tt_generation;
}

bool Engine::_tb_legal(const GameState& state) const {
    if (state.empty_count < 0 || state.empty_count > ENDGAME_EMPTY) return false;
    if (state.placed_this_turn < 0 || state.placed_this_turn > state.max_this_turn) {
        return false;
    }
    if (state.score_at_turn_start < 1 || state.score_at_turn_start > NUM_CELLS) {
        return false;
    }
    if (state.allows_three && state.max_this_turn < 3) return false;
    if (state.score_increased[state.to_move]
        && state.largest[state.to_move] < state.score_at_turn_start) {
        return false;
    }
    return true;
}

bool Engine::_tb_lookup(std::uint64_t key, int ply, int& value, int& packed) const {
    auto found = tablebase.find(key);
    if (found == tablebase.end()) return false;
    value = score_from_tt(found->second.first, ply);
    packed = found->second.second;
    return true;
}

int Engine::_history_row() const {
    const GameState& s = *st;
    bool gift = false;
    if (history_gift_split) {
        int me = s.to_move;
        bool grew = s.largest[me] > s.score_at_turn_start
                    && s.largest[me] >= s.largest[1 - me];
        gift = grew || (s.placed_this_turn == 0 && s.allows_three);
    }
    return hist_slot(s.placed_this_turn, gift);
}

void Engine::_hist_update(int& cell_hist, int bonus) {
    cell_hist += bonus - cell_hist * std::abs(bonus) / HIST_MAX;
}

int Engine::_lmr_reduction(int depth, int searched, int kind, int cell) const {
    int d = depth < LMR_TABLE_N ? depth : LMR_TABLE_N - 1;
    int s = searched < LMR_TABLE_N ? searched : LMR_TABLE_N - 1;
    if (d < 0) d = 0;
    if (s < 0) s = 0;
    int reduction = LMR_TABLE[d][s];
    if (d >= 1 && s >= 1 && std::abs(lmr_divisor - 1.8) > 1e-12) {
        double r = std::log(static_cast<double>(d))
                   * std::log(static_cast<double>(s)) / lmr_divisor;
        reduction = r < 0 ? 0 : static_cast<int>(r);
    }
    if (kind_is_crit(kind)) return 0;
    int cap = 3;
    if (kind == KIND_EXPAND) {
        cap = depth >= 10 ? 2 : 1;
    } else if (kind == KIND_HARRASS) {
        reduction += 1;
        cap = depth >= 10 ? 4 : 2;
    } else if (kind == KIND_QUIET) {
        reduction += 1;
        if (cell >= 0 && CENTRALITY[cell] >= 3) reduction -= 1;
        cap = depth >= 10 ? lmr_quiet_cap : 3;
    }
    if (reduction < 1) reduction = 1;
    if (reduction > cap) reduction = cap;
    return reduction;
}

void Engine::_reset_counters() {
    tt_probes = 0;
    tt_hits = 0;
    tt_cutoffs = 0;
    first_fail_high = 0;
    lmr_reduced = 0;
    lmr_researched = 0;
    lmp_dropped = 0;
    nmp_cuts = 0;
    nodes_end_turn = 0;
    std::memset(nodes_kind, 0, sizeof(nodes_kind));
}

Engine::Engine(bool use_tt_, bool use_pvs_, bool use_killers_, bool use_history_,
               bool use_aspiration_, bool use_lmr_, bool use_opening_book_,
               int max_depth_, int aspiration_delta_,
               int w_largest_, int w_sumsq_, int w_frontier_, int w_centre_,
               int w_tempo_, int w_pending_, int w_liberty_, int w_stones_,
               int w_second_, int w_join_, int w_crit_, int w_third_,
               int w_fourth_, bool use_rfp_, bool use_lmp_, bool use_iid_,
               int w_gift_, int w_reachable_, int tt_bits_,
               bool use_countermove_, bool use_stable_stop_,
               double lmr_divisor_, int lmr_quiet_cap_)
    : st(nullptr),
      tt_bits(clamp_tt_bits(tt_bits_)),
      tt_buckets(1 << tt_bits),
      tt_mask(tt_buckets - 1),
      tt_generation(1),
      nodes(0),
      deadline(0.0),
      ws(std::make_unique<EngineWorkspace>()),
      pv(ws->pv),
      pv_len(ws->pv_len),
      killer0(ws->killer0),
      killer1(ws->killer1),
      history(ws->history),
      counter_move(ws->counter_move),
      counter_hist(ws->counter_hist),
      ply_prev(ws->ply_prev),
      move_buf(ws->move_buf),
      move_kind(ws->move_kind),
      move_len(ws->move_len),
      buckets(ws->buckets),
      bucket_n(ws->bucket_n),
      cell_own_gain(ws->cell_own_gain),
      cell_opp_gain(ws->cell_opp_gain),
      use_tt(use_tt_),
      use_pvs(use_pvs_),
      use_killers(use_killers_),
      use_history(use_history_),
      use_aspiration(use_aspiration_),
      use_lmr(use_lmr_),
      lmr_divisor(lmr_divisor_),
      lmr_quiet_cap(lmr_quiet_cap_),
      use_opening_book(use_opening_book_),
      use_rfp(use_rfp_),
      use_lmp(use_lmp_),
      use_iid(use_iid_),
      use_countermove(use_countermove_),
      use_stable_stop(use_stable_stop_),
      max_depth(max_depth_),
      aspiration_delta(aspiration_delta_),
      w_largest(w_largest_),
      w_sumsq(w_sumsq_),
      w_frontier(w_frontier_),
      w_centre(w_centre_),
      w_tempo(w_tempo_),
      w_pending(w_pending_),
      w_liberty(w_liberty_),
      w_stones(w_stones_),
      w_second(w_second_),
      w_join(w_join_),
      w_crit(w_crit_),
      w_third(w_third_),
      w_fourth(w_fourth_),
      w_gift(w_gift_),
      w_reachable(w_reachable_),
      _hit_depth(0),
      _hit_value(0),
      _hit_flag(0),
      _hit_pack(0),
      _in_iid(false),
      gift_ext_used(false),
      nmp_ok(true) {
    init_tt();
    std::memset(pv, 0, sizeof(pv));
    std::memset(pv_len, 0, sizeof(pv_len));
    for (int i = 0; i < MAX_PLY; ++i) {
        killer0[i] = -1;
        killer1[i] = -1;
        move_len[i] = 0;
        ply_prev[i] = -1;
    }
    std::memset(history, 0, sizeof(history));
    std::memset(counter_hist, 0, sizeof(counter_hist));
    for (int i = 0; i < NUM_CELLS; ++i) counter_move[i] = -1;
    std::memset(move_buf, 0, sizeof(move_buf));
    std::memset(move_kind, 0, sizeof(move_kind));
    std::memset(buckets, 0, sizeof(ws->buckets));
    std::memset(bucket_n, 0, sizeof(ws->bucket_n));
    std::memset(cell_own_gain, 0, sizeof(ws->cell_own_gain));
    std::memset(cell_opp_gain, 0, sizeof(ws->cell_opp_gain));
    _reset_counters();
}

int Engine::_evaluate(int placements_left, int ply) {
    GameState& s = *st;
    int me = s.to_move;
    int opp = 1 - me;
    int mine = s.largest[me];
    int theirs = s.largest[opp];
    int empty = s.empty_count;
    int my_lr = -1;
    int opp_lr = -1;

    if (empty && mine > theirs + empty) {
        if (mine > s.max_reachable(opp)) return WIN_SCORE - ply;
    }
    if (empty && theirs > mine + empty) {
        if (theirs > s.max_reachable(me)) return ply - WIN_SCORE;
    }

    std::uint64_t empty_bb = FULL_MASK ^ (s.bb[0] | s.bb[1]);
    int best_own = 0;
    int best_opp = 0;
    int join_me = 0;
    int join_opp = 0;
    int vol_best = 0;
    int vol_big = 0;
    if (w_crit || w_gift || w_join || w_pending) {
        std::uint64_t cand = (s.touch[me] | s.touch[opp]) & empty_bb;
        while (cand) {
            std::uint64_t bit = cand & -cand;
            int x = ctz(bit);
            cand ^= bit;
            int og = placement_gain(s, x, me);
            int ed = placement_gain(s, x, opp);
            if (og > best_own) best_own = og;
            if (ed > best_opp) best_opp = ed;
            if (w_join) {
                join_me += joined_group_size(s, x, me);
                join_opp += joined_group_size(s, x, opp);
            }
            int swing = og > ed ? og : ed;
            if (swing > vol_best) vol_best = swing;
            if (swing >= 2) vol_big += 1;
        }
    }
    if (vol_big >= 2) vol_best += vol_best ? vol_best : 2;
    int pending_w = w_pending;
    if (empty <= ENDGAME_EMPTY) pending_w = 0;
    else if (vol_best >= 2) pending_w = pending_w * 3 / 2;

    int score =
        w_largest * (mine - theirs)
        + w_sumsq * (s.sumsq[me] - s.sumsq[opp])
        + w_frontier * (s.frontier[me] - s.frontier[opp])
        + w_centre * (s.centrality[me] - s.centrality[opp])
        + w_liberty * (s.largest_liberties(me) - s.largest_liberties(opp))
        + pending_w * placements_left
        + w_stones * (s.nstones[me] - s.nstones[opp]);
    int max_k = placements_left > 0 ? placements_left : 0;
    if (max_k >= 2) {
        BridgeInfo br = own_bridges(s, me, max_k);
        if (br.gain > best_own) best_own = br.gain;
    }

    int margin = mine - theirs;
    if (w_reachable && empty) {
        my_lr = s.largest_reachable(me);
        opp_lr = s.largest_reachable(opp);
        int dead_w = w_reachable < REACHABLE_DEAD_WEIGHT
                         ? w_reachable : REACHABLE_DEAD_WEIGHT;
        int w = (opp_lr >= mine) ? w_reachable : dead_w;
        score += w * (my_lr - opp_lr);
    }
    if (use_race) {
        int step = race_advantage(s, placements_left);
        if (step && w_largest > 0) step = soft_saturate(step, w_largest);
        score += step;
    }
    bool race = empty > 14 && empty <= 45;
    // join_me sums the sizes of groups a cell would connect. Divide by 32 so
    // the default weight still prefers a larger join, without outscoring the
    // largest-group term on a short tactical line. A non-zero sum that
    // rounds away is kept as one point so a small join still registers.
    int join_raw = join_me - join_opp;
    int join_term = w_join * join_raw / 32;
    if (join_term == 0 && join_raw != 0 && w_join != 0) {
        join_term = ((join_raw > 0) == (w_join > 0)) ? 1 : -1;
    }
    int second_term = w_second * (s.second[me] - s.second[opp]);
    int crit_term = w_crit * (best_own - best_opp);
    // 1-stone opponent joins (gain >= 2) are the F7-class threats. Double
    // that side of crit in midgame, then cap so it cannot outweigh a
    // 3-point largest-group lead.
    if (best_opp >= 2 && empty > 14) crit_term -= w_crit * best_opp;
    if (margin <= -2) {
        join_term = join_term * 3 / 2;
        second_term = second_term * 4 / 5;
        crit_term = crit_term * 5 / 4;
    } else if (margin >= 2 && race) {
        join_term /= 2;
    }
    if (w_largest > 0) {
        crit_term = soft_saturate(crit_term, w_largest * 3);
    }
    score += join_term + second_term;
    if (w_crit) score += crit_term;
    if (use_unique) score += unique_connector_term(s, w_largest);

    int phase = VEC_EMPTY_START - empty;
    if (phase < 0) phase = 0;
    if (phase > VEC_PHASE_SPAN) phase = VEC_PHASE_SPAN;
    if (phase && (w_third || w_fourth)) {
        int g3 = group_nth(s, me, 3) - group_nth(s, opp, 3);
        int g4 = group_nth(s, me, 4) - group_nth(s, opp, 4);
        score += (w_third * g3 + w_fourth * g4) * phase / VEC_PHASE_SPAN;
    }

    int gift = 0;
    if (s.placed_this_turn == 0 && s.allows_three) {
        gift = w_gift * best_own * empty / 40;
    } else if (mine > s.score_at_turn_start && mine >= theirs) {
        int denied_best = 0;
        int nplace = s.placed_this_turn;
        if (nplace > s._undo_n) nplace = s._undo_n;
        for (int i = 0; i < nplace; ++i) {
            int cell = s._u_cell[s._undo_n - 1 - i];
            int denied = placement_gain(s, cell, opp);
            if (denied > denied_best) denied_best = denied;
        }
        int own_grow = mine - s.score_at_turn_start;
        bool cut_gift = denied_best >= CRIT_GAIN && denied_best > own_grow;
        bool merged_existing = own_grow > nplace;

        gift = -w_gift * best_opp * empty / 40;
        gift -= w_tempo * empty / 40;
        // Growing by 1 to deny a multi-group merge is not a dump.
        if (cut_gift) gift /= 2;
        if (!cut_gift && !merged_existing) {
            if (opp_lr < 0) opp_lr = s.largest_reachable(opp);
            if (opp_lr >= mine) {
                int pocket_left = opp_lr - theirs;
                if (pocket_left > 0 && pocket_left * 2 <= empty) {
                    int dump = pocket_left > 3 ? 3 : pocket_left;
                    score -= w_largest * dump;
                }
            }
        }
    }
    // The post-growth branch already charged the gift. This catches the turn
    // where every legal stone still grows an ahead side, before any of them
    // is played.
    if (use_forced_gift && s.placed_this_turn == 0
        && !(mine > s.score_at_turn_start && mine >= theirs)
        && forced_to_gift(s)) {
        gift -= w_gift * best_opp * empty / 40;
    }
    if (gift) {
        int num = 1;
        int den = 1;
        if (empty > 45 || empty <= 14) {
            den = 2;
        } else if (margin <= -2) {
            den = 3;
        } else if (margin <= 1) {
            den = 2;
        }
        gift = gift * num / den;
        if (w_largest > 0) gift = soft_saturate(gift, w_largest);
        score += gift;
    }
    return clamp_eval(score);
}

int Engine::_cell_kind(int cell, int me, int opp, std::uint64_t lib_me,
                      std::uint64_t lib_opp, int ahead) {
    GameState& s = *st;
    bool ahead_b;
    if (lib_me == UINT64_MAX) {
        lib_opp = s.largest_lib_mask(opp);
        ahead_b = (s.empty_count > LATE_MERGE_EMPTY
                   && s.score_at_turn_start > s.largest[opp]);
    } else {
        ahead_b = ahead != 0;
    }
    (void)lib_me;
    int og = placement_gain(s, cell, me);
    int ed = placement_gain(s, cell, opp);
    bool touches_own = s.adjacency[me][cell] != 0;
    bool touches_opp = s.adjacency[opp][cell] != 0;
    // Own-adjacent cells that deny a bigger opponent merge (F7) are cuts
    // while ahead. When catching up, keep MERGE so a +1 grow is not stolen
    // by a +2 opponent denial.
    if (ed >= DENIAL_BLOCK && ed > og && (!touches_own || ahead_b)) {
        return KIND_BLOCK;
    }
    if (touches_own) {
        if (!ahead_b && (og >= 1 || own_group_count(s, cell, me) >= 2)) {
            return KIND_MERGE;
        }
        if (!ahead_b && s.placements_left() >= 2) {
            BridgeInfo br = own_bridges(s, me, s.placements_left());
            if (br.cells & CELL_BIT[cell]) return KIND_MERGE;
        }
        return KIND_EXPAND;
    }
    if (!ahead_b && s.placements_left() >= 2) {
        BridgeInfo br = own_bridges(s, me, s.placements_left());
        if (br.cells & CELL_BIT[cell]) return KIND_MERGE;
    }
    if (ed >= DENIAL_BLOCK) return KIND_BLOCK;
    if (lib_opp & CELL_BIT[cell]) {
        if (s.largest_reachable(opp) >= s.largest[me]) return KIND_BLOCK;
    }
    if (touches_opp) return KIND_HARRASS;
    return KIND_QUIET;
}

int Engine::_cell_kind_find(int cell, int me, int opp) {
    return _cell_kind(cell, me, opp);
}

int Engine::_fill_moves(int min_index, int hash_move, int ply, bool q_only,
                        bool q_expand, int out_off) {
    GameState& s = *st;
    int me = s.to_move;
    int opp = 1 - me;
    int* buf = move_buf[ply];
    int* kinds = move_kind[ply];
    int n = out_off;

    int killer_a = use_killers ? killer0[ply] : -1;
    int killer_b = use_killers ? killer1[ply] : -1;
    int prev = ply_prev[ply];

    bool ahead = (s.empty_count > LATE_MERGE_EMPTY
                  && s.score_at_turn_start > s.largest[opp]);
    std::uint64_t lib_opp = s.largest_lib_mask(opp);
    bool need_reach =
        s.empty_count <= 20 || s.largest[me] <= s.largest[opp] + 2;
    if (need_reach && s.largest_reachable(opp) < s.largest[me]) lib_opp = 0;

    std::uint64_t occupied = s.bb[0] | s.bb[1];
    std::uint64_t empty_all = FULL_MASK ^ occupied;
    std::uint64_t empty = empty_all & INDEX_MASK_FROM[min_index];
    int max_k = s.placements_left();
    BridgeInfo br{0, 0, 0};
    if (!ahead && max_k >= 1) br = own_bridges(s, me, max_k);
    // After a stone this turn, a lower-index cell can complete a join
    // (C1 after D1 E2). Include those cells despite min_index.
    if (s.placed_this_turn >= 1 && min_index > 0 && br.cells) {
        std::uint64_t below = empty_all & ~INDEX_MASK_FROM[min_index] & FULL_MASK;
        empty |= below & br.cells;
    }
    std::uint64_t touch_me = s.touch[me] & empty;
    std::uint64_t touch_opp = s.touch[opp] & empty;

    int cell_swing[NUM_CELLS];
    std::memset(cell_swing, 0, sizeof(cell_swing));
    std::memset(cell_own_gain[ply], 0, sizeof(ws->cell_own_gain[ply]));
    std::memset(cell_opp_gain[ply], 0, sizeof(ws->cell_opp_gain[ply]));
    std::uint64_t merge = 0;
    std::uint64_t deny = 0;
    std::uint64_t cand = touch_me | touch_opp;
    while (cand) {
        std::uint64_t bit = cand & -cand;
        int x = ctz(bit);
        cand ^= bit;
        int og = placement_gain(s, x, me);
        int ed = placement_gain(s, x, opp);
        cell_own_gain[ply][x] = og;
        cell_opp_gain[ply][x] = ed;
        cell_swing[x] = denial_swing(og, ed, swing_asymmetric);
        if (ed >= DENIAL_BLOCK && ed > og && (!(touch_me & bit) || ahead)) {
            deny |= bit;
        } else if (touch_me & bit) {
            if (og >= 1 || (!ahead && own_group_count(s, x, me) >= 2)) {
                merge |= bit;
            }
        } else if (ed >= DENIAL_BLOCK) {
            deny |= bit;
        }
    }
    std::uint64_t expand;
    if (ahead) {
        expand = touch_me & ~deny;
        merge = 0;
    } else {
        expand = touch_me & ~merge & ~deny;
        if (!q_only && max_k >= 2 && br.cells) {
            std::uint64_t bcells = br.cells & empty;
            merge |= bcells;
            expand &= ~merge;
            std::uint64_t walk = br.starters & empty;
            while (walk) {
                std::uint64_t bit = walk & -walk;
                int x = ctz(bit);
                walk ^= bit;
                if (cell_own_gain[ply][x] < br.gain) {
                    cell_own_gain[ply][x] = br.gain;
                    cell_swing[x] = denial_swing(cell_own_gain[ply][x],
                                                 cell_opp_gain[ply][x],
                                                 swing_asymmetric);
                }
            }
        }
    }
    std::uint64_t block = ((lib_opp & ~touch_me) | deny) & empty & ~merge;
    std::uint64_t harass = touch_opp & ~touch_me & ~block & ~merge;
    std::uint64_t quiet = empty & ~touch_me & ~touch_opp & ~merge;

    if (q_only && s.placed_this_turn == 0) {
        auto keep_crit = [&](std::uint64_t mask) {
            std::uint64_t out = 0;
            while (mask) {
                std::uint64_t bit = mask & -mask;
                int x = ctz(bit);
                mask ^= bit;
                if (cell_own_gain[ply][x] >= CRIT_GAIN) out |= bit;
            }
            return out;
        };
        merge = keep_crit(merge);
        block = keep_crit(block);
        expand = 0;
        harass = 0;
        quiet = 0;
    }

    // Kind bits before symmetry reduction. A hash cell can be the
    // non-canonical image of its orbit; it is still that kind of move.
    std::uint64_t raw_merge = merge;
    std::uint64_t raw_expand = expand;
    std::uint64_t raw_block = block;
    std::uint64_t raw_harass = harass;

    if (s.placed_this_turn == 0 && min_index == 0) {
        int live[N_SYMMETRIES];
        int nlive = _live_symmetries(live);
        if (nlive >= 2) {
            auto keep_canon = [&](std::uint64_t mask) {
                std::uint64_t out = 0;
                while (mask) {
                    std::uint64_t bit = mask & -mask;
                    int x = ctz(bit);
                    mask ^= bit;
                    if (_is_canonical_cell(x, live, nlive)) out |= bit;
                }
                return out;
            };
            merge = keep_canon(merge);
            expand = keep_canon(expand);
            block = keep_canon(block);
            harass = keep_canon(harass);
            quiet = keep_canon(quiet);
        }
    }

    if (hash_move >= 0) {
        std::uint64_t hbit = CELL_BIT[hash_move];
        bool hang_q = q_only && s.placed_this_turn == 0;
        if ((empty & hbit) && (!hang_q || ((merge | block) & hbit))) {
            buf[n] = hash_move;
            kinds[n] = kind_from_masks(hbit, raw_merge, raw_expand, raw_block,
                                       raw_harass);
            n += 1;
            merge &= ~hbit;
            expand &= ~hbit;
            block &= ~hbit;
            harass &= ~hbit;
            quiet &= ~hbit;
        }
    }
    int bucket_at = n;

    if (q_only && q_expand) {
        std::uint64_t kept = 0;
        std::uint64_t mask = expand;
        while (mask) {
            std::uint64_t bit = mask & -mask;
            int x = ctz(bit);
            mask ^= bit;
            if (cell_opp_gain[ply][x] > 0
                || (ahead && cell_own_gain[ply][x] >= 1)) {
                kept |= bit;
            }
        }
        expand = kept;
    }

    int* counts = bucket_n[ply];
    for (int k = 0; k < N_BUCKETS; ++k) counts[k] = 0;

    counts[KIND_MERGE] = extract_bits(merge, buckets[ply][KIND_MERGE]);
    counts[KIND_BLOCK] = extract_bits(block, buckets[ply][KIND_BLOCK]);
    if (!q_only || q_expand) {
        counts[KIND_EXPAND] = extract_bits(expand, buckets[ply][KIND_EXPAND]);
    }
    if (!q_only) {
        counts[KIND_HARRASS] = extract_bits(harass, buckets[ply][KIND_HARRASS]);
        counts[KIND_QUIET] = extract_bits(quiet, buckets[ply][KIND_QUIET]);
    }

    int order_hist[NUM_CELLS];
    if (use_history) {
        std::memcpy(order_hist, history[me][_history_row()], sizeof(order_hist));
    } else {
        std::memset(order_hist, 0, sizeof(order_hist));
    }
    if (!q_only && use_countermove && prev >= 0 && prev < NUM_CELLS) {
        for (int c = 0; c < NUM_CELLS; ++c) {
            order_hist[c] += counter_hist[prev][c];
        }
    }
    const int* hist = order_hist;
    bool hist_on = !q_only && (use_history || use_killers || use_countermove);
    insertion_sort_swing(buckets[ply][KIND_MERGE], 0, counts[KIND_MERGE],
                         cell_swing, hist, hist_on);
    insertion_sort_swing(buckets[ply][KIND_BLOCK], 0, counts[KIND_BLOCK],
                         cell_swing, hist, hist_on);
    insertion_sort_swing(buckets[ply][KIND_EXPAND], 0, counts[KIND_EXPAND],
                         cell_swing, hist, hist_on);
    insertion_sort_swing(buckets[ply][KIND_HARRASS], 0, counts[KIND_HARRASS],
                         cell_swing, hist, hist_on);
    insertion_sort_swing(buckets[ply][KIND_QUIET], 0, counts[KIND_QUIET],
                         cell_swing, hist, hist_on);

    for (int kind = 0; kind < N_BUCKETS; ++kind) {
        int* span = buckets[ply][kind];
        for (int i = 0; i < counts[kind]; ++i) {
            buf[n] = span[i];
            kinds[n] = kind;
            n += 1;
        }
    }

    // Validate killers and the countermove against the generated list, then
    // pull each legal one in front of the buckets. Critical promotion below
    // still keeps a critical gain ahead of them.
    if (!q_only) {
        int at = bucket_at;
        auto try_special = [&](int cell) {
            if (cell < 0 || cell >= NUM_CELLS) return;
            if (cell == hash_move) return;
            for (int i = at; i < n; ++i) {
                if (buf[i] != cell) continue;
                int held = kinds[i];
                for (int j = i; j > at; --j) {
                    buf[j] = buf[j - 1];
                    kinds[j] = kinds[j - 1];
                }
                buf[at] = cell;
                kinds[at] = held;
                at += 1;
                return;
            }
        };
        try_special(killer_a);
        try_special(killer_b);
        int cm = -1;
        if (use_countermove && prev >= 0 && prev < NUM_CELLS) {
            cm = counter_move[prev];
        }
        try_special(cm);
    }

    // Stable-promote critical gains ahead of the remaining buckets. The TT
    // cell, if any, stays in front. END_TURN lives before bucket_at.
    int crit_cell[MOVE_CAP];
    int crit_kind[MOVE_CAP];
    int rest_cell[MOVE_CAP];
    int rest_kind[MOVE_CAP];
    int ncrit = 0;
    int nrest = 0;
    for (int i = bucket_at; i < n; ++i) {
        int cell = buf[i];
        int kind = kinds[i];
        int og = (cell >= 0 && cell < NUM_CELLS) ? cell_own_gain[ply][cell] : 0;
        int ed = (cell >= 0 && cell < NUM_CELLS) ? cell_opp_gain[ply][cell] : 0;
        bool crit = kind >= 0 && (og >= CRIT_GAIN || (ed >= CRIT_GAIN && ed > og));
        if (crit) {
            crit_cell[ncrit] = cell;
            crit_kind[ncrit] = kind;
            ncrit += 1;
        } else {
            rest_cell[nrest] = cell;
            rest_kind[nrest] = kind;
            nrest += 1;
        }
    }
    for (int i = 0; i < ncrit; ++i) {
        buf[bucket_at + i] = crit_cell[i];
        kinds[bucket_at + i] = crit_kind[i];
    }
    for (int i = 0; i < nrest; ++i) {
        buf[bucket_at + ncrit + i] = rest_cell[i];
        kinds[bucket_at + ncrit + i] = rest_kind[i];
    }

    for (int i = 0; i < n; ++i) {
        int cell = buf[i];
        if (cell < 0 || cell >= NUM_CELLS) continue;
        int og = cell_own_gain[ply][cell];
        int ed = cell_opp_gain[ply][cell];
        if (kinds[i] == KIND_MERGE && og >= CRIT_GAIN) kinds[i] = KIND_CRIT_MERGE;
        else if (kinds[i] == KIND_BLOCK && ed >= CRIT_GAIN) kinds[i] = KIND_CRIT_BLOCK;
    }

    move_len[ply] = n;
    return n;
}

bool Engine::_end_turn_allowed(int placements_left, int placed) {
    GameState& s = *st;
    if (placements_left <= 0 || s.empty_count == 0) return true;
    if (placed < 1) return false;
    if (s.empty_count <= ENDGAME_EMPTY) return true;
    int me = s.to_move;
    // Passing leftover stones is legal, but midgame it is almost always a
    // blunder. Only stop early once this turn already gifted.
    bool increased = s.largest[me] > s.score_at_turn_start;
    return increased && s.largest[me] >= s.largest[1 - me];
}

int Engine::_insert_end_turn(int ply, int n) {
    return _insert_end_turn_at(ply, n, n);
}

int Engine::_insert_end_turn_at(int ply, int pos, int n) {
    int* buf = move_buf[ply];
    int* kinds = move_kind[ply];
    for (int i = n; i > pos; --i) {
        buf[i] = buf[i - 1];
        kinds[i] = kinds[i - 1];
    }
    buf[pos] = END_TURN;
    kinds[pos] = KIND_END_TURN;
    move_len[ply] = n + 1;
    return n + 1;
}

int Engine::_fill_actions(int min_index, int hash_move, int ply,
                          int placements_left, int placed, int hint_pack,
                          int hint_off, bool q_only) {
    bool gift = false;
    bool hint_done = false;
    if (placed >= 1) {
        GameState& s = *st;
        int me = s.to_move;
        bool score_up = s.largest[me] > s.score_at_turn_start;
        gift = score_up && s.largest[me] >= s.largest[1 - me];
        hint_done = hint_pack != 0 && hint_off >= (hint_pack & 3);
    }

    bool allow_end;
    int reserve;
    int n;
    if (placements_left > 0) {
        allow_end = placed >= 1 && _end_turn_allowed(placements_left, placed);
        // Start-of-turn qsearch only resolves hanging MERGE/BLOCK joins;
        // expanding every liberty here explodes the horizon.
        bool q_expand = q_only && !allow_end && placed >= 1;
        // TT-hinted turn-end keeps slot 0. Gift-stop does not: leftover MERGE /
        // BLOCK / EXPAND stones still grow largest after the gift is committed.
        reserve = (allow_end && hint_done) ? 1 : 0;
        if (reserve) {
            move_buf[ply][0] = END_TURN;
            move_kind[ply][0] = KIND_END_TURN;
        }
        n = _fill_moves(min_index, hash_move, ply, q_only, q_expand, reserve);
    } else {
        n = 0;
        move_len[ply] = 0;
        allow_end = true;
        reserve = 0;
    }

    if (placed >= 1 && (allow_end || n == reserve)) {
        if (!reserve) {
            if (gift) {
                int pos = 0;
                for (int i = 0; i < n; ++i) {
                    int kind = move_kind[ply][i];
                    if (kind_is_merge(kind) || kind_is_block(kind)
                        || kind == KIND_EXPAND) {
                        pos = i + 1;
                    }
                }
                n = _insert_end_turn_at(ply, pos, n);
            } else {
                n = _insert_end_turn(ply, n);
            }
        }
        move_len[ply] = n;
    }
    return n;
}

std::vector<int> Engine::_ordered_cells(int min_index, int hash_move, int ply) {
    int n = _fill_moves(min_index, hash_move, ply);
    return std::vector<int>(move_buf[ply], move_buf[ply] + n);
}

std::uint64_t Engine::_tt_key(const GameState* state, int min_index) const {
    const GameState* s = state ? state : st;
    return get_canonical_key(*s, min_index);
}

std::uint64_t Engine::_place_child_key(int cell) const {
    const GameState& s = *st;
    GameState child = s.clone();
    child.place(cell);
    return get_canonical_key(child, cell + 1);
}

std::uint64_t Engine::_end_turn_child_key() const {
    GameState child = st->clone();
    child.end_turn();
    return get_canonical_key(child, 0);
}

int Engine::_live_symmetries(int* live) const {
    const GameState& s = *st;
    live[0] = 0;
    int n = 1;
    for (int g = 1; g < N_SYMMETRIES; ++g) {
        bool ok = true;
        for (int c = 0; c < NUM_CELLS; ++c) {
            int img = SYM.perm[g][c];
            if (img < 0 || s.colour[c] != s.colour[img]) {
                ok = false;
                break;
            }
        }
        if (ok) live[n++] = g;
    }
    return n;
}

bool Engine::_is_canonical_cell(int cell, const int* live, int nlive) const {
    int canon = cell;
    for (int i = 0; i < nlive; ++i) {
        int img = SYM.perm[live[i]][cell];
        if (img < canon) canon = img;
    }
    return canon == cell;
}

bool Engine::_etc_cutoff(int depth, int beta, int placements_left, int min_index,
                         int ply, int placed, int& value, int& action) {
    if (!use_tt || ply <= 0 || depth <= 1) return false;
    const GameState& s = *st;
    int needed_place = depth - 1;
    if (placements_left > 0) {
        std::uint64_t empty =
            (FULL_MASK ^ (s.bb[0] | s.bb[1])) & INDEX_MASK_FROM[min_index];
        for (int i = 0; i < NUM_CELLS; ++i) {
            int cell = STATIC_ORDER[i];
            if (!(empty & CELL_BIT[cell])) continue;
            if (!_probe(_place_child_key(cell))) continue;
            if (_hit_depth < needed_place) continue;
            if (_hit_flag == UPPER_BOUND) continue;
            int child_value = score_from_tt(_hit_value, ply + 1);
            if (child_value >= beta) {
                value = child_value;
                action = cell;
                return true;
            }
        }
    }
    if (placed >= 1 && _end_turn_allowed(placements_left, placed)) {
        int needed_end = end_turn_child_depth(depth, false, ply,
                                              predicted_next_turn_max(s),
                                              gift_ext_used);
        if (_probe(_end_turn_child_key()) && _hit_depth >= needed_end
            && _hit_flag != LOWER_BOUND) {
            int child_value = -score_from_tt(_hit_value, ply + 1);
            if (child_value >= beta) {
                value = child_value;
                action = END_TURN;
                return true;
            }
        }
    }
    return false;
}

bool Engine::_probe(std::uint64_t key) {
    tt_probes += 1;
    int idx = tt_index(key);
    tt_lock(idx);
    std::uint64_t chk = tt_chk(key);
    const TTSlot& dslot = tt_d[idx];
    const TTSlot& aslot = tt_a[idx];
    int d_depth = dslot.depth;
    bool hit_d = tt_slot_live(dslot) && dslot.key == chk;
    int a_depth = aslot.depth;
    bool hit_a = tt_slot_live(aslot) && aslot.key == chk;
    if (hit_d && (!hit_a || d_depth >= a_depth)) {
        _hit_depth = d_depth;
        _hit_value = dslot.value;
        _hit_flag = dslot.flag;
        _hit_pack = dslot.turn;
        tt_hits += 1;
        tt_unlock(idx);
        return true;
    }
    if (hit_a) {
        _hit_depth = a_depth;
        _hit_value = aslot.value;
        _hit_flag = aslot.flag;
        _hit_pack = aslot.turn;
        tt_hits += 1;
        tt_unlock(idx);
        return true;
    }
    tt_unlock(idx);
    return false;
}

bool Engine::tt_lookup(std::uint64_t key, int& depth, int& value, int& flag,
                       std::vector<int>& turn) {
    if (!_probe(key)) return false;
    depth = _hit_depth;
    value = _hit_value;
    flag = _hit_flag;
    int pack = _hit_pack;
    // Entries are stored in the canonical frame; if the key is the current
    // state's turn-start key, return the cells in that state's own frame.
    int sym = 0;
    if (st && get_canonical_key(*st, 0, &sym) == key) pack = pack_from_canonical(pack, sym);
    turn = unpack_cells(pack);
    return true;
}

void Engine::_write_tt(std::uint64_t key, int depth, int value, int flag, int packed) {
    int idx = tt_index(key);
    tt_lock(idx);
    std::uint64_t chk = tt_chk(key);
    TTSlot incoming;
    incoming.key = chk;
    incoming.depth = depth;
    incoming.value = value;
    incoming.turn = packed;
    incoming.flag = flag;
    incoming.gen = tt_generation;

    TTSlot& dslot = tt_d[idx];
    TTSlot& aslot = tt_a[idx];
    bool d_live = tt_slot_live(dslot);
    bool a_live = tt_slot_live(aslot);
    bool d_same = d_live && dslot.key == chk;
    bool a_same = a_live && aslot.key == chk;

    if (d_same) {
        if (depth >= dslot.depth) dslot = incoming;
        else aslot = incoming;
        tt_unlock(idx);
        return;
    }
    if (a_same) {
        if (!d_live || depth >= dslot.depth) dslot = incoming;
        else aslot = incoming;
        tt_unlock(idx);
        return;
    }
    if (tt_keep_deep && d_live && dslot.depth >= depth + 2) {
        tt_unlock(idx);
        return;
    }
    if (!d_live) {
        dslot = incoming;
        tt_unlock(idx);
        return;
    }
    if (!a_live) {
        aslot = incoming;
        tt_unlock(idx);
        return;
    }
    if (depth >= dslot.depth) {
        if (dslot.depth > aslot.depth) aslot = dslot;
        dslot = incoming;
        tt_unlock(idx);
        return;
    }
    if (depth >= aslot.depth) aslot = incoming;
    if (aslot.depth > dslot.depth) {
        TTSlot tmp = dslot;
        dslot = aslot;
        aslot = tmp;
    }
    tt_unlock(idx);
}

void Engine::_write_tt(std::uint64_t key, int depth, int value, int flag,
                       const std::vector<int>& turn) {
    int n = static_cast<int>(turn.size());
    if (n > 3) n = 3;
    int c0 = n > 0 ? turn[0] : 0;
    int c1 = n > 1 ? turn[1] : 0;
    int c2 = n > 2 ? turn[2] : 0;
    _write_tt(key, depth, value, flag, pack_cells(c0, c1, c2, n));
}

void Engine::_store(std::uint64_t key, int depth, int value, int alpha_original,
                    int beta, int ply, int best_action, int sym) {
    int flag;
    if (value <= alpha_original) flag = UPPER_BOUND;
    else if (value >= beta) flag = LOWER_BOUND;
    else flag = EXACT;

    int n = pv_len[ply];
    int c0 = 0, c1 = 0, c2 = 0, count = 0;
    int i = 0;
    while (i < n && count < 3) {
        int a = pv[ply][i];
        if (a < 0) break;
        if (count == 0) c0 = a;
        else if (count == 1) c1 = a;
        else c2 = a;
        count += 1;
        i += 1;
    }
    if (count == 0 && best_action >= 0) {
        count = 1;
        c0 = best_action;
    }
    int packed = pack_to_canonical(pack_cells(c0, c1, c2, count), sym);
    int stored_value = score_to_tt(value, ply);
    _write_tt(key, depth, stored_value, flag, packed);
    if (flag == EXACT && st && st->empty_count <= ENDGAME_EMPTY
        && _tb_legal(*st)) {
        tablebase[key] = {stored_value, packed};
    }
}

void Engine::_copy_pv(int ply, int action) {
    pv[ply][0] = action;
    int n = pv_len[ply + 1];
    for (int i = 0; i < n; ++i) pv[ply][i + 1] = pv[ply + 1][i];
    pv_len[ply] = n + 1;
}

void Engine::_load_pv(int ply, int pack) {
    std::vector<int> cells = unpack_cells(pack);
    int n = static_cast<int>(cells.size());
    for (int i = 0; i < n; ++i) pv[ply][i] = cells[i];
    pv_len[ply] = n;
}

int Engine::_negamax(int depth, int alpha, int beta, int placements_left,
                     int min_index, int ply, int hint_pack, int hint_off) {
    GameState& s = *st;
    int empty = s.empty_count;
    if (empty == 0) {
        pv_len[ply] = 0;
        int ts = s.terminal_score(s.to_move);
        if (ts > 0) return WIN_SCORE - ply;
        if (ts < 0) return ply - WIN_SCORE;
        return 0;
    }

    // Key (and the symmetry of its canonical frame) is computed lazily: most
    // quiescence leaves return before they need it.
    int tt_sym = 0;
    std::uint64_t tt_key = 0;
    bool have_key = false;

    if (empty <= ENDGAME_EMPTY && _tb_legal(s)) {
        tt_key = get_canonical_key(s, min_index, &tt_sym);
        have_key = true;
        int tb_value = 0;
        int tb_pack = 0;
        if (_tb_lookup(tt_key, ply, tb_value, tb_pack)) {
            pv_len[ply] = 0;
            tb_pack = pack_from_canonical(tb_pack, tt_sym);
            if (tb_pack) _load_pv(ply, tb_pack);
            return tb_value;
        }
    }

    int me_now = s.to_move;
    int mine = s.largest[me_now];
    int theirs = s.largest[1 - me_now];
    if (ply > 0 && empty) {
        if (mine > theirs + empty && mine > s.max_reachable(1 - me_now)) {
            pv_len[ply] = 0;
            return WIN_SCORE - ply;
        }
        if (theirs > mine + empty && theirs > s.max_reachable(me_now)) {
            pv_len[ply] = 0;
            return ply - WIN_SCORE;
        }
    }

    nodes += 1;
    if (!(nodes & 2047) && now() >= deadline.load(std::memory_order_relaxed)) {
        throw TimeoutException();
    }

    int placed = s.placed_this_turn;
    bool exact = exact_solve_position(s);
    bool q_only = false;

    if (depth <= 0 && !exact) {
        if (placed == 0) {
            // Resolve a hanging 1-ply merge/join before standing pat. That is
            // the F7-class threat: leaving it in eval understates the swing.
            if (best_placement_gain(s, me_now) < CRIT_GAIN) {
                pv_len[ply] = 0;
                return _evaluate(placements_left, ply);
            }
        }
        q_only = true;
    }

    pv_len[ply] = 0;
    int alpha_original = alpha;
    if (!have_key) tt_key = get_canonical_key(s, min_index, &tt_sym);

    if (use_tt && _probe(tt_key)) {
        int stored_depth = _hit_depth;
        int stored_value = score_from_tt(_hit_value, ply);
        int stored_flag = _hit_flag;
        int stored_pack = pack_from_canonical(_hit_pack, tt_sym);
        if (hint_pack == 0 && stored_pack) {
            hint_pack = stored_pack;
            hint_off = 0;
        }
        if (stored_depth >= depth && ply > 0) {
            bool cutoff = false;
            if (stored_flag == EXACT) cutoff = true;
            else if (stored_flag == LOWER_BOUND) cutoff = stored_value >= beta;
            else cutoff = stored_value <= alpha;
            if (cutoff) {
                tt_cutoffs += 1;
                _load_pv(ply, stored_pack);
                return stored_value;
            }
        }
    }

    if (use_rfp && !exact && !q_only && ply > 0 && placed == 0
        && depth <= RFP_MAX_DEPTH
        && beta < MATE_THRESHOLD && beta > -MATE_THRESHOLD) {
        bool gifted = mine > s.score_at_turn_start && mine >= theirs;
        if (!gifted && merge_volatility(s) < 2) {
            int eval = _evaluate(placements_left, ply);
            int margin = RFP_BASE + RFP_DEPTH * depth;
            if (eval - margin >= beta) return eval;
        }
    }

    // A pass is not legal at placed == 0, but it is a lower bound: any real
    // stone is at least as good as giving the turn away. Skip the stones this
    // turn would have spent, then reduce. Two passes in a row are refused.
    bool may_null = nmp_ok;
    nmp_ok = true;
    if (use_nmp && may_null && !exact && !q_only && ply > 0 && placed == 0
        && depth >= 4 && s.empty_count > EXACT_EMPTY_MAX
        && beta < MATE_THRESHOLD && beta > -MATE_THRESHOLD) {
        int skip = s.max_this_turn;
        int reduction = 2 + depth / 6;
        int null_depth = depth - skip - reduction;
        if (skip >= 1 && null_depth >= 1) {
            s.end_turn();
            int child_placements = s.max_this_turn;
            nmp_ok = false;
            ply_prev[ply + 1] = -1;
            int null_value = 0;
            try {
                null_value = -_negamax(null_depth, -beta, -beta + 1,
                                       child_placements, 0, ply + 1, 0, 0);
            } catch (...) {
                nmp_ok = true;
                s.unend_turn();
                throw;
            }
            nmp_ok = true;
            s.unend_turn();
            if (null_value >= beta) {
                nmp_cuts += 1;
                pv_len[ply] = 0;
                if (use_tt) {
                    _store(tt_key, depth, null_value, alpha_original, beta, ply, -1, tt_sym);
                }
                return null_value;
            }
        }
    }

    if (!q_only) {
        int etc_value = 0;
        int etc_action = END_TURN;
        if (_etc_cutoff(depth, beta, placements_left, min_index, ply, placed,
                        etc_value, etc_action)) {
            pv[ply][0] = etc_action;
            pv_len[ply] = 1;
            if (use_tt) {
                _store(tt_key, depth, etc_value, alpha_original, beta, ply, etc_action,
                       tt_sym);
            }
            return etc_value;
        }
    }

    int hash_n = hint_pack & 3;
    int hash_move = (hint_off < hash_n) ? ((hint_pack >> (2 + 6 * hint_off)) & 63) : -1;

    if (use_iid && !_in_iid && use_tt && !exact && !q_only && ply > 0
        && placed == 0 && depth >= IID_MIN_DEPTH && hash_move < 0) {
        _in_iid = true;
        int iid_depth = depth - IID_REDUCTION;
        if (iid_depth < 1) iid_depth = 1;
        try {
            (void)_negamax(iid_depth, alpha, beta, placements_left,
                           min_index, ply, 0, 0);
        } catch (...) {
            _in_iid = false;
            throw;
        }
        _in_iid = false;
        if (_probe(tt_key) && _hit_pack) {
            hint_pack = pack_from_canonical(_hit_pack, tt_sym);
            hint_off = 0;
            hash_n = hint_pack & 3;
            hash_move = (hint_off < hash_n)
                ? ((hint_pack >> (2 + 6 * hint_off)) & 63) : -1;
        }
    }

    int n_actions = _fill_actions(min_index, hash_move, ply, placements_left,
                                  placed, hint_pack, hint_off, q_only);
    if (n_actions == 0) {
        pv_len[ply] = 0;
        return _evaluate(placements_left, ply);
    }
    int* actions = move_buf[ply];
    int* kinds = move_kind[ply];

    int killer_a = use_killers ? killer0[ply] : -1;
    int killer_b = use_killers ? killer1[ply] : -1;
    int prev = ply_prev[ply];
    int cm = (use_countermove && prev >= 0 && prev < NUM_CELLS)
                 ? counter_move[prev] : -1;

    if (use_lmp && !exact && !q_only && depth <= LMP_MAX_DEPTH
        && n_actions > LMP_MIN_ACTIONS) {
        int quiet_cap = depth <= 1 ? LMP_QUIET_D1 : LMP_QUIET_D2;
        if (lmp_by_placements) {
            int scale = placements_left > 1 ? placements_left : 1;
            quiet_cap *= scale;
        }
        int kept_quiet = 0;
        int w = 0;
        int hplaced = _history_row();
        for (int i = 0; i < n_actions; ++i) {
            if (kinds[i] == KIND_QUIET) {
                int cell = actions[i];
                bool protected_quiet =
                    cell == hash_move || cell == killer_a || cell == killer_b
                    || (cm >= 0 && cell == cm)
                    || (use_history && history[me_now][hplaced][cell] > 0);
                if (!protected_quiet) {
                    bool bad_hist = use_history
                        && history[me_now][hplaced][cell] < -(HIST_MAX / 4);
                    if (bad_hist || kept_quiet >= quiet_cap) {
                        lmp_dropped += 1;
                        continue;
                    }
                    kept_quiet += 1;
                }
            }
            if (w != i) {
                actions[w] = actions[i];
                kinds[w] = kinds[i];
            }
            w += 1;
        }
        n_actions = w;
        move_len[ply] = w;
    }
    bool use_pvs_flag = use_pvs;
    bool use_lmr_flag = use_lmr && !exact && !q_only;
    int me = me_now;
    int opp = 1 - me;
    int best = -INFINITY_SCORE;
    int best_action = END_TURN;
    int searched = 0;
    int crit_ext = 0;
    int value = 0;

    for (int i = 0; i < n_actions; ++i) {
        int action = actions[i];
        int kind = kinds[i];
        bool passive_gift = false;
        int bucket = kind_bucket(kind);
        if (bucket >= 0 && bucket < N_BUCKETS) nodes_kind[bucket] += 1;
        else if (kind == KIND_END_TURN) nodes_end_turn += 1;
        if (action == END_TURN) {
            bool denied_cut = false;
            int nplace = s.placed_this_turn;
            int un = s._undo_n;
            if (nplace > un) nplace = un;
            for (int i = 0; i < nplace; ++i) {
                int cell = s._u_cell[un - 1 - i];
                if (placement_gain(s, cell, opp) >= CRIT_GAIN) {
                    denied_cut = true;
                    break;
                }
            }
            s.end_turn();
            bool extend = false;
            try {
                int child_placements = s.max_this_turn;
                extend = !gift_ext_used && !denied_cut && !q_only
                         && child_placements >= 3 && depth >= 3
                         && ply < EXTEND_PLY_CAP;
                int child_depth = end_turn_child_depth(depth, q_only, ply,
                                                       child_placements,
                                                       gift_ext_used || denied_cut);
                if (extend) gift_ext_used = true;
                ply_prev[ply + 1] = -1;
                value = _pvs_end_turn(child_depth, alpha, beta, child_placements,
                                      ply, searched, use_pvs_flag);
            } catch (...) {
                if (extend) gift_ext_used = false;
                s.unend_turn();
                throw;
            }
            if (extend) gift_ext_used = false;
            s.unend_turn();
        } else {
            int og = cell_own_gain[ply][action];
            int ed = cell_opp_gain[ply][action];
            passive_gift = ply == 0 && history_gift_split && og > 0
                           && s.largest[me] + og >= s.largest[opp]
                           && ed >= CRIT_GAIN;
            bool critical = og >= CRIT_GAIN || ed >= CRIT_GAIN;
            bool forcing = kind_is_merge(kind) || kind_is_block(kind) || critical
                           || kind_is_crit(kind);
            bool reduce = (use_lmr_flag && searched >= 3 && depth >= 3
                           && action != hash_move && action != killer_a
                           && action != killer_b && !forcing);
            if (reduce && ply == 0 && kind < KIND_HARRASS) reduce = false;
            int old_largest = s.largest[me];
            s.place(action);
            try {
                int child_off, child_pack;
                if (action == hash_move) {
                    child_off = hint_off + 1;
                    child_pack = hint_pack;
                } else {
                    child_pack = 0;
                    child_off = 0;
                }
                bool increased = s.largest[me] > old_largest;
                bool grants_three = increased && s.largest[me] >= s.largest[opp];
                if (reduce && increased) reduce = false;
                int child_depth = 0;
                if (q_only && placed == 0 && og >= CRIT_GAIN) {
                    ply_prev[ply + 1] = action;
                    pv_len[ply + 1] = 0;
                    value = _evaluate(placements_left - 1, ply);
                } else if (q_only) {
                    child_depth = 0;
                    ply_prev[ply + 1] = action;
                    value = _pvs_place(child_depth, alpha, beta, placements_left - 1,
                                       action, ply, child_pack, child_off,
                                       searched, use_pvs_flag);
                } else {
                    child_depth = depth - 1;
                    bool want_extend = increased && !grants_three && ply < EXTEND_PLY_CAP;
                    if (want_extend && searched == 0) {
                        if (depth < SINGULAR_MIN_DEPTH) {
                            child_depth = depth;
                        } else if (_singular_extend(depth, alpha, beta, action,
                                                    placements_left, ply,
                                                    child_pack, child_off,
                                                    actions, n_actions, i)) {
                            child_depth = depth;
                        }
                    }
                    if (critical && !grants_three && crit_ext < CRIT_EXTEND_CAP
                        && ply < EXTEND_PLY_CAP) {
                        reduce = false;
                        if (child_depth < 1) {
                            child_depth = 1;
                            crit_ext += 1;
                        }
                    }
                    if (reduce) {
                        int reduction = _lmr_reduction(depth, searched, kind, action);
                        int reduced_depth = child_depth - reduction;
                        if (reduced_depth < 1) reduced_depth = 1;
                        lmr_reduced += 1;
                        ply_prev[ply + 1] = action;
                        value = _negamax(reduced_depth, alpha, alpha + 1,
                                         placements_left - 1, action + 1, ply + 1,
                                         child_pack, child_off);
                        if (value > alpha) {
                            lmr_researched += 1;
                            ply_prev[ply + 1] = action;
                            value = _negamax(child_depth, alpha, beta,
                                             placements_left - 1, action + 1, ply + 1,
                                             child_pack, child_off);
                        }
                    } else {
                        ply_prev[ply + 1] = action;
                        value = _pvs_place(child_depth, alpha, beta, placements_left - 1,
                                           action, ply, child_pack, child_off,
                                           searched, use_pvs_flag);
                    }
                }
            } catch (...) {
                s.unplace();
                throw;
            }
            s.unplace();
        }

        searched += 1;
        bool better = value > best;
        if (!better && value == best && action != END_TURN
            && best_action == END_TURN) {
            better = true;
        }
        if (action != END_TURN && use_history && value <= alpha_original
            && (kind == KIND_HARRASS || kind == KIND_QUIET)) {
            int d = depth > 0 ? depth : 1;
            _hist_update(history[s.to_move][_history_row()][action], -(d * d));
        }
        if (better) {
            bool raised = value > best;
            best = value;
            best_action = action;
            if (value > alpha) {
                alpha = value;
                _copy_pv(ply, action);
                if (alpha >= beta) {
                    if (searched == 1) first_fail_high += 1;
                    if (action != END_TURN) {
                        if (use_killers && killer0[ply] != action) {
                            killer1[ply] = killer0[ply];
                            killer0[ply] = action;
                        }
                        if (use_history) {
                            int d = depth > 0 ? depth : 1;
                            int bonus = d * d;
                            if (passive_gift) bonus = std::max(1, bonus / 2);
                            _hist_update(history[s.to_move][_history_row()][action], bonus);
                        }
                        int prev_act = ply_prev[ply];
                        if (use_countermove && prev_act >= 0 && prev_act < NUM_CELLS) {
                            counter_move[prev_act] = action;
                            int d = depth > 0 ? depth : 1;
                            _hist_update(counter_hist[prev_act][action], d * d);
                        }
                    }
                    break;
                }
            } else if (!raised) {
                _copy_pv(ply, action);
            }
        }
    }
    if (use_tt) {
        _store(tt_key, depth, best, alpha_original, beta, ply, best_action, tt_sym);
    }
    return best;
}

int Engine::_pvs_place(int child_depth, int alpha, int beta, int placements_left,
                       int action, int ply, int hint_pack, int hint_off,
                       int searched, bool use_pvs_flag) {
    ply_prev[ply + 1] = action;
    if (searched && use_pvs_flag) {
        int value = _negamax(child_depth, alpha, alpha + 1, placements_left,
                             action + 1, ply + 1, hint_pack, hint_off);
        if (alpha < value && value < beta) {
            ply_prev[ply + 1] = action;
            value = _negamax(child_depth, alpha, beta, placements_left,
                             action + 1, ply + 1, hint_pack, hint_off);
        }
        return value;
    }
    return _negamax(child_depth, alpha, beta, placements_left,
                    action + 1, ply + 1, hint_pack, hint_off);
}

int Engine::_pvs_end_turn(int depth, int alpha, int beta, int child_placements,
                          int ply, int searched, bool use_pvs_flag) {
    ply_prev[ply + 1] = -1;
    if (searched && use_pvs_flag) {
        int value = -_negamax(depth, -alpha - 1, -alpha, child_placements,
                              0, ply + 1, 0, 0);
        if (alpha < value && value < beta) {
            ply_prev[ply + 1] = -1;
            value = -_negamax(depth, -beta, -alpha, child_placements,
                              0, ply + 1, 0, 0);
        }
        return value;
    }
    return -_negamax(depth, -beta, -alpha, child_placements, 0, ply + 1, 0, 0);
}

std::vector<int> Engine::_sanitise_turn(const GameState& state,
                                        const std::vector<int>& turn) {
    std::vector<int> legal;
    bool seen[NUM_CELLS] = {};
    int cap = state.max_this_turn;
    for (int cell : turn) {
        if (cell < 0 || cell >= NUM_CELLS || state.colour[cell] || seen[cell]) continue;
        legal.push_back(cell);
        seen[cell] = true;
        if (static_cast<int>(legal.size()) >= cap) break;
    }
    GameState* saved = st;
    GameState probe = state.clone();
    st = &probe;
    try {
        if (legal.empty()) {
            _fill_moves(0, -1, 0);
            legal.push_back(move_buf[0][0]);
            seen[move_buf[0][0]] = true;
        }
        for (int cell : legal) probe.place(cell);
        while (static_cast<int>(legal.size()) < cap && probe.empty_count > 0) {
            bool must = !_end_turn_allowed(probe.placements_left(),
                                           probe.placed_this_turn);
            int me = probe.to_move;
            int next = -1;
            int probe_sym = 0;
            std::uint64_t probe_key =
                get_canonical_key(probe, 1 + legal.back(), &probe_sym);
            if (use_tt && _probe(probe_key) && _hit_pack) {
                std::vector<int> more =
                    unpack_cells(pack_from_canonical(_hit_pack, probe_sym));
                for (int c : more) {
                    if (c >= 0 && c < NUM_CELLS && !probe.colour[c] && !seen[c]) {
                        next = c;
                        break;
                    }
                }
            }
            if (next >= 0 && !must && placement_gain(probe, next, me) < 1) {
                next = -1;
            }
            if (next < 0) {
                _fill_moves(0, -1, 0);
                if (move_len[0] <= 0) break;
                next = move_buf[0][0];
            }
            if (!must && placement_gain(probe, next, me) < 1) {
                BridgeInfo more = own_bridges(probe, me, probe.placements_left());
                if (!(more.starters & CELL_BIT[next])) break;
            }
            legal.push_back(next);
            seen[next] = true;
            probe.place(next);
        }
    } catch (...) {
        st = saved;
        throw;
    }
    st = saved;
    return legal;
}

int Engine::_seed_aspiration() const {
    int vol = merge_volatility(*st);
    if (vol <= 1) {
        int tight = aspiration_delta / 2;
        return tight < 40 ? 40 : tight;
    }
    return aspiration_delta + w_largest * vol;
}

bool Engine::_singular_extend(int depth, int alpha, int beta, int action,
                              int placements_left, int ply, int hint_pack,
                              int hint_off, const int* actions, int n_actions,
                              int index) {
    GameState& s = *st;
    int rdepth = depth - SINGULAR_REDUCTION;
    if (rdepth < 1) rdepth = 1;
    ply_prev[ply + 1] = action;
    int first = _negamax(rdepth, alpha, beta, placements_left - 1,
                         action + 1, ply + 1, hint_pack, hint_off);
    int second_action = END_TURN - 1;
    for (int j = index + 1; j < n_actions; ++j) {
        second_action = actions[j];
        break;
    }
    if (second_action == END_TURN - 1) return true;
    s.unplace();
    int second;
    try {
        if (second_action == END_TURN) {
            s.end_turn();
            try {
                ply_prev[ply + 1] = -1;
                second = -_negamax(rdepth, -beta, -alpha, s.max_this_turn,
                                   0, ply + 1, 0, 0);
            } catch (...) {
                s.unend_turn();
                throw;
            }
            s.unend_turn();
        } else {
            s.place(second_action);
            try {
                ply_prev[ply + 1] = second_action;
                second = _negamax(rdepth, alpha, beta, placements_left - 1,
                                  second_action + 1, ply + 1, 0, 0);
            } catch (...) {
                s.unplace();
                throw;
            }
            s.unplace();
        }
    } catch (...) {
        s.place(action);
        throw;
    }
    s.place(action);
    int margin = w_largest > 80 ? w_largest : 80;
    return first >= second + margin;
}

int Engine::_search_root(int depth, int root_placements, int hint_pack,
                         int previous_value) {
    gift_ext_used = false;
    nmp_ok = true;
    if (!use_aspiration || depth < 3 || std::abs(previous_value) >= MATE_THRESHOLD) {
        return _negamax(depth, -INFINITY_SCORE, INFINITY_SCORE, root_placements,
                        0, 0, hint_pack, 0);
    }
    int seed = _seed_aspiration();
    int delta = seed;
    while (true) {
        int alpha = previous_value - delta;
        int beta = previous_value + delta;
        int value = _negamax(depth, alpha, beta, root_placements, 0, 0, hint_pack, 0);
        if (alpha < value && value < beta) return value;
        delta *= 4;
        if (delta > 8 * seed) {
            return _negamax(depth, -INFINITY_SCORE, INFINITY_SCORE,
                            root_placements, 0, 0, hint_pack, 0);
        }
    }
}

std::pair<std::vector<int>, SearchStats> Engine::find_turn(
    GameState& state, double time_limit, ReportFn report) {
    double started = now();
    st = &state;
    double budget = time_limit;
    if (move_wall > 0.0 && budget > move_wall) budget = move_wall;
    deadline.store(started + std::max(0.01, budget), std::memory_order_relaxed);
    nodes = 0;
    gift_ext_used = false;
    nmp_ok = true;
    pv_len[0] = 0;
    ply_prev[0] = -1;
    _reset_counters();

    SearchStats stats;

    if (use_opening_book && state.turn_index == 0 && state.empty_count == NUM_CELLS) {
        stats.turn = {CENTRE_CELL};
        stats.depth = 0;
        stats.elapsed = now() - started;
        return {stats.turn, stats};
    }

    _fill_moves(0, -1, 0);
    std::vector<int> best_turn = {move_buf[0][0]};
    int best_value = 0;
    int root_placements = state.max_this_turn;
    int hint_pack = 0;
    // A sub-0.2 s budget is the panic slice from Clock::allocate. Skip the
    // aspiration loop and stable-stop so the one iteration that fits is kept.
    bool panic = time_limit < 0.2;

    auto search_depth = [&](int depth) {
        int value;
        if (panic) {
            value = _negamax(depth, -INFINITY_SCORE, INFINITY_SCORE,
                             root_placements, 0, 0, hint_pack, 0);
        } else {
            value = _search_root(depth, root_placements, hint_pack, best_value);
        }
        int n = pv_len[0];
        std::vector<int> turn;
        for (int i = 0; i < n; ++i) {
            int a = pv[0][i];
            if (a < 0) break;
            turn.push_back(a);
        }
        if (!turn.empty()) {
            best_turn = turn;
            int nn = static_cast<int>(turn.size());
            if (nn > 3) nn = 3;
            hint_pack = nn;
            if (nn > 0) hint_pack |= turn[0] << 2;
            if (nn > 1) hint_pack |= turn[1] << 8;
            if (nn > 2) hint_pack |= turn[2] << 14;
        }
        best_value = value;
        stats.depth = depth;
        stats.value = value;
        stats.completed = depth;
        if (report) {
            report(depth, value, best_turn, nodes, now() - started);
        }
        return value;
    };

    try {
        int last = std::min(max_depth, state.empty_count);
        int own_turns_left = std::max(1, static_cast<int>(state.empty_count / 3.6));
        double stop_frac = 0.90;
        if (own_turns_left <= 3 || state.empty_count <= 20) {
            stop_frac = 0.98;
        }
        int stable = 0;
        int last_move = -1;
        int last_value = 0;
        int vol = merge_volatility(state);
        bool allow_stable = use_stable_stop && !panic && vol <= 1
                            && own_turns_left > 3 && state.empty_count > 20
                            && state.max_this_turn < 3;
        for (int depth = 1; depth <= last; ++depth) {
            int value = search_depth(depth);
            if (value > MATE_THRESHOLD) break;
            int first = best_turn.empty() ? -1 : best_turn[0];
            int bound = w_largest > 0 ? w_largest / 2 : aspiration_delta;
            if (first == last_move && std::abs(value - last_value) <= bound) {
                stable += 1;
            } else {
                stable = 0;
            }
            last_move = first;
            last_value = value;
            if (allow_stable && stable >= 3 && depth >= 10) break;
            double elapsed = now() - started;
            if (elapsed > stop_frac * budget) break;
        }
    } catch (const TimeoutException&) {
        // keep last completed depth
    }

    stats.nodes = nodes;
    stats.elapsed = now() - started;
    stats.turn = _sanitise_turn(state, best_turn);
    stats.tt_probes = tt_probes;
    stats.tt_hits = tt_hits;
    stats.tt_cutoffs = tt_cutoffs;
    stats.first_fail_high = first_fail_high;
    stats.lmr_reduced = lmr_reduced;
    stats.lmr_researched = lmr_researched;
    stats.lmp_dropped = lmp_dropped;
    stats.nmp_cuts = nmp_cuts;
    stats.nodes_end_turn = nodes_end_turn;
    std::memcpy(stats.nodes_kind, nodes_kind, sizeof(nodes_kind));
    return {stats.turn, stats};
}

void Engine::new_game() {
    tt_generation = static_cast<std::uint8_t>(tt_generation + 1);
    if (tt_generation == 0) {
        init_tt();
    }
    std::memset(history, 0, sizeof(history));
    std::memset(counter_hist, 0, sizeof(counter_hist));
    for (int i = 0; i < NUM_CELLS; ++i) counter_move[i] = -1;
    for (int i = 0; i < MAX_PLY; ++i) {
        killer0[i] = -1;
        killer1[i] = -1;
        ply_prev[i] = -1;
    }
}

}  // namespace catchup
