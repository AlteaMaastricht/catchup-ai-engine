#pragma once

#include "state.hpp"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <unordered_map>
#include <vector>

namespace catchup {

inline constexpr int INFINITY_SCORE = 1 << 30;
inline constexpr int END_TURN = -1;
inline constexpr int EXACT = 0;
inline constexpr int LOWER_BOUND = 1;
inline constexpr int UPPER_BOUND = 2;
inline constexpr int MAX_PLY = 160;
inline constexpr int MOVE_CAP = 64;
inline constexpr int N_BUCKETS = 5;
inline constexpr int TT_BITS = 20;
inline constexpr int TT_BITS_MIN = 12;
inline constexpr int TT_BITS_MAX = 24;
inline constexpr int TT_BUCKETS = 1 << TT_BITS;
inline constexpr int TT_MASK = TT_BUCKETS - 1;
inline constexpr int HIST_MAX = 10000;
// Sort-key only. Large enough that a killer wins an equal-swing tie against
// a saturated history score, without crossing a higher swing.
inline constexpr int KILLER_BONUS = 4 * HIST_MAX;
inline constexpr int HIST_PLACED = 4;
// Rows 0..3 are placed-this-turn. Rows 4..7 repeat that split for a gift turn.
inline constexpr int HIST_ROWS = HIST_PLACED * 2;
inline constexpr int LMR_TABLE_N = 64;
inline constexpr int SINGULAR_MIN_DEPTH = 6;
inline constexpr int SINGULAR_REDUCTION = 3;
inline constexpr int EXTEND_PLY_CAP = 24;

// Gifted 3-stone replies cost one extra placement. Keep leftover depth after
// that stone in line with a normal 2-stone exchange, once per gift turn.
inline int end_turn_child_depth(int depth, bool q_only, int ply,
                                int next_placements,
                                bool already_extended = false) {
    if (q_only) return 0;
    if (!already_extended && next_placements >= 3 && depth >= 3
        && ply < EXTEND_PLY_CAP) {
        return depth + 1;
    }
    return depth;
}

inline int predicted_next_turn_max(const GameState& s) {
    bool increased = s.largest[s.to_move] > s.score_at_turn_start;
    bool next_three = increased && s.largest[s.to_move] >= s.largest[1 - s.to_move];
    int cap = next_three ? 3 : 2;
    return cap < s.empty_count ? cap : s.empty_count;
}

inline std::uint64_t tt_checksum(std::uint64_t key, int bits = TT_BITS) {
    return key >> bits;
}

// Forced wins occupy WIN_SCORE down to WIN_SCORE - MAX_PLY. Heuristic eval
// is clamped below that so the 0..10000000 window stays unambiguous.
inline constexpr int MATE_THRESHOLD = WIN_SCORE - MAX_PLY;
inline constexpr int EVAL_MAX = MATE_THRESHOLD - 1;

inline int clamp_eval(int score) {
    if (score > EVAL_MAX) return EVAL_MAX;
    if (score < -EVAL_MAX) return -EVAL_MAX;
    return score;
}

inline int score_to_tt(int score, int ply) {
    if (score > MATE_THRESHOLD) return score + ply;
    if (score < -MATE_THRESHOLD) return score - ply;
    return score;
}

inline int score_from_tt(int score, int ply) {
    if (score > MATE_THRESHOLD) return score - ply;
    if (score < -MATE_THRESHOLD) return score + ply;
    return score;
}

inline int hist_slot(int placed, bool gift = false) {
    int row;
    if (placed < 0) row = 0;
    else if (placed >= HIST_PLACED) row = HIST_PLACED - 1;
    else row = placed;
    if (gift) row += HIST_PLACED;
    return row;
}
inline constexpr int ENDGAME_EMPTY = 10;
inline constexpr int EXACT_EMPTY_MAX = 12;
inline constexpr int EXACT_TACTICAL_MAX = 8;
inline constexpr int LATE_MERGE_EMPTY = 10;
inline constexpr int CRIT_GAIN = 2;
inline constexpr int CRIT_EXTEND_CAP = 4;
inline constexpr int VEC_EMPTY_START = 20;
inline constexpr int VEC_PHASE_SPAN = 12;
inline constexpr int DENIAL_BLOCK = 2;
inline constexpr int REACHABLE_DEAD_WEIGHT = 2;
// One step when the larger disjoint pocket is strictly faster or slower.
inline constexpr int RACE_WEIGHT = 40;
inline constexpr int RACE_EMPTY_MAX = 40;
inline constexpr int RACE_SHARED_MAX = 4;
// Must-answer merge: one empty cell joins a secondary group to the largest.
inline constexpr int UNIQUE_WEIGHT = 48;
inline constexpr int UNIQUE_CAP = 2;
inline constexpr int RFP_MAX_DEPTH = 4;
inline constexpr int RFP_BASE = 80;
inline constexpr int RFP_DEPTH = 70;
inline constexpr int LMP_MAX_DEPTH = 2;
inline constexpr int LMP_QUIET_D1 = 8;
inline constexpr int LMP_QUIET_D2 = 16;
inline constexpr int LMP_MIN_ACTIONS = 16;
inline constexpr int IID_MIN_DEPTH = 6;
inline constexpr int IID_REDUCTION = 2;

inline constexpr int KIND_MERGE = 0;
inline constexpr int KIND_BLOCK = 1;
inline constexpr int KIND_EXPAND = 2;
inline constexpr int KIND_HARRASS = 3;
inline constexpr int KIND_QUIET = 4;
// Not bucket ids. Assigned after the bucket sort so LMR can see a critical
// merge or block without giving it the quiet-move reduction cap.
inline constexpr int KIND_CRIT_MERGE = 5;
inline constexpr int KIND_CRIT_BLOCK = 6;
inline constexpr int KIND_END_TURN = -1;

inline bool kind_is_merge(int kind) {
    return kind == KIND_MERGE || kind == KIND_CRIT_MERGE;
}
inline bool kind_is_block(int kind) {
    return kind == KIND_BLOCK || kind == KIND_CRIT_BLOCK;
}
inline bool kind_is_crit(int kind) {
    return kind == KIND_CRIT_MERGE || kind == KIND_CRIT_BLOCK;
}
inline int kind_bucket(int kind) {
    if (kind == KIND_CRIT_MERGE) return KIND_MERGE;
    if (kind == KIND_CRIT_BLOCK) return KIND_BLOCK;
    return kind;
}

// Lazy SMP is deferred: the packed TT is a prerequisite, but Engine search
// state (move_buf, killers, history, st) is still single-threaded. Enable
// only if the match setting allows N cores, and share this TT via atomics.
inline int clamp_tt_bits(int bits) {
    if (bits < TT_BITS_MIN) return TT_BITS_MIN;
    if (bits > TT_BITS_MAX) return TT_BITS_MAX;
    return bits;
}

class TimeoutException : public std::runtime_error {
public:
    TimeoutException() : std::runtime_error("search timeout") {}
};

struct SearchStats {
    int nodes = 0;
    int depth = 0;
    int value = 0;
    double elapsed = 0.0;
    std::vector<int> turn;
    int completed = 0;
    int tt_probes = 0;
    int tt_hits = 0;
    int tt_cutoffs = 0;
    int first_fail_high = 0;
    int lmr_reduced = 0;
    int lmr_researched = 0;
    int lmp_dropped = 0;
    int nmp_cuts = 0;
    int nodes_kind[N_BUCKETS] = {};
    int nodes_end_turn = 0;

    double nps() const { return elapsed > 0.0 ? nodes / elapsed : 0.0; }
    std::string describe() const;
};

class Clock {
public:
    double total;
    double reserve;
    double used;
    // Stones removed between consecutive own turns (our move plus the reply).
    // One virtual sample keeps the 3.6 prior until real cycles arrive.
    double avg_cycle = 3.6;
    int cycle_samples = 1;
    int pending_own = -1;

    explicit Clock(double total_time = 600.0, double reserve_time = 8.0)
        : total(total_time), reserve(reserve_time), used(0.0) {}

    double remaining() const { return total - reserve - used; }
    double allocate(const GameState& state) const;
    void consume(double seconds) { used += seconds; }
    void note_own_stones(int stones);
    void note_reply_stones(int stones);
    void undo_note();

private:
    struct CycleSnap {
        double avg_cycle;
        int cycle_samples;
        int pending_own;
    };
    std::vector<CycleSnap> cycle_undo;
};

struct TTSlot {
    std::uint64_t key = 0;
    int depth = -1;
    int value = 0;
    int turn = 0;
    std::uint8_t flag = 0;
    std::uint8_t gen = 0;
};

struct TTLock {
    std::atomic_flag held = ATOMIC_FLAG_INIT;

    TTLock() = default;
    TTLock(const TTLock&) { held.clear(std::memory_order_relaxed); }
    TTLock& operator=(const TTLock&) {
        held.clear(std::memory_order_relaxed);
        return *this;
    }
};

struct EngineWorkspace {
    int pv[MAX_PLY][MAX_PLY];
    int pv_len[MAX_PLY];
    int killer0[MAX_PLY];
    int killer1[MAX_PLY];
    int history[2][HIST_ROWS][NUM_CELLS];
    int counter_move[NUM_CELLS];
    int counter_hist[NUM_CELLS][NUM_CELLS];
    int ply_prev[MAX_PLY];
    int move_buf[MAX_PLY][MOVE_CAP];
    int move_kind[MAX_PLY][MOVE_CAP];
    int move_len[MAX_PLY];
    int buckets[MAX_PLY][N_BUCKETS][MOVE_CAP];
    int bucket_n[MAX_PLY][N_BUCKETS];
    int cell_own_gain[MAX_PLY][NUM_CELLS];
    int cell_opp_gain[MAX_PLY][NUM_CELLS];
};

class Engine {
public:
    GameState* st;

    std::vector<TTSlot> tt_d, tt_a;
    std::vector<TTLock> tt_locks;
    int tt_bits;
    int tt_buckets;
    int tt_mask;
    std::uint8_t tt_generation;
    std::unordered_map<std::uint64_t, std::pair<int, int>> tablebase;

    int nodes;
    std::atomic<double> deadline;
    std::unique_ptr<EngineWorkspace> ws;
    int (&pv)[MAX_PLY][MAX_PLY];
    int (&pv_len)[MAX_PLY];
    int (&killer0)[MAX_PLY];
    int (&killer1)[MAX_PLY];
    int (&history)[2][HIST_ROWS][NUM_CELLS];
    int (&counter_move)[NUM_CELLS];
    int (&counter_hist)[NUM_CELLS][NUM_CELLS];
    int (&ply_prev)[MAX_PLY];
    int (&move_buf)[MAX_PLY][MOVE_CAP];
    int (&move_kind)[MAX_PLY][MOVE_CAP];
    int (&move_len)[MAX_PLY];
    int (&buckets)[MAX_PLY][N_BUCKETS][MOVE_CAP];
    int (&bucket_n)[MAX_PLY][N_BUCKETS];
    int (&cell_own_gain)[MAX_PLY][NUM_CELLS];
    int (&cell_opp_gain)[MAX_PLY][NUM_CELLS];

    int tt_probes;
    int tt_hits;
    int tt_cutoffs;
    int first_fail_high;
    int lmr_reduced;
    int lmr_researched;
    int lmp_dropped;
    int nmp_cuts;
    int nodes_kind[N_BUCKETS];
    int nodes_end_turn;

    bool use_tt;
    bool use_pvs;
    bool use_killers;
    bool use_history;
    bool use_aspiration;
    bool use_lmr;
    // Table is built at LMR_DIVISOR. A different value is computed per call
    // so a match can sweep the divisor without also moving the quiet cap.
    double lmr_divisor = 1.8;
    // Quiet reductions at depth >= 10. Depths below that stay capped at 3.
    int lmr_quiet_cap = 5;
    bool use_opening_book;
    bool use_rfp;
    // Null-move pruning: a pass at the start of a turn is a lower bound.
    bool use_nmp = true;
    bool use_lmp;
    bool use_iid;
    bool use_countermove;
    bool use_stable_stop;
    // Depth-preferred TT slot is left alone when it is already depth+2.
    bool tt_keep_deep = false;
    // Quiet LMP cap multiplied by max(1, placements still to make).
    bool lmp_by_placements = false;
    // Swing key 2*own_gain + 3*opp_gain. Off keeps the denial-weighted key.
    bool swing_asymmetric = false;
    // History rows split by whether this turn is a gift.
    bool history_gift_split = true;
    // Disjoint-pocket turn step. Off leaves the linear reachable term alone.
    bool use_race = false;
    // Clean unique connectors between a secondary group and the largest.
    bool use_unique = false;
    // Penalty when every empty cell would grow an ahead side to move.
    bool use_forced_gift = false;
    // Hard per-move ceiling. 0 disables the cap.
    double move_wall = 45.0;
    int max_depth;
    int aspiration_delta;
    int w_largest, w_sumsq, w_frontier, w_centre, w_tempo;
    int w_pending, w_liberty, w_stones, w_second, w_join;
    int w_crit, w_third, w_fourth, w_gift, w_reachable;

    int _hit_depth, _hit_value, _hit_flag, _hit_pack;

    Engine(bool use_tt_ = true,
           bool use_pvs_ = true,
           bool use_killers_ = true,
           bool use_history_ = true,
           bool use_aspiration_ = true,
           bool use_lmr_ = true,
           bool use_opening_book_ = true,
           int max_depth_ = 64,
           int aspiration_delta_ = 120,
           int w_largest_ = 100,
           int w_sumsq_ = 2,
           int w_frontier_ = 1,
           int w_centre_ = 2,
           int w_tempo_ = 0,
           int w_pending_ = 25,
           int w_liberty_ = 8,
           int w_stones_ = 20,
           int w_second_ = 40,
           int w_join_ = 4,
           int w_crit_ = 16,
           int w_third_ = 25,
           int w_fourth_ = 8,
           bool use_rfp_ = true,
           bool use_lmp_ = true,
           bool use_iid_ = false,
           int w_gift_ = 80,
           int w_reachable_ = 10,
           int tt_bits_ = TT_BITS,
           bool use_countermove_ = true,
           bool use_stable_stop_ = true,
           double lmr_divisor_ = 1.8,
           int lmr_quiet_cap_ = 5);

    int _evaluate(int placements_left, int ply = 0);
    int _cell_kind(int cell, int me, int opp, std::uint64_t lib_me = UINT64_MAX,
                   std::uint64_t lib_opp = UINT64_MAX, int ahead = -1);
    int _cell_kind_find(int cell, int me, int opp);
    int _fill_moves(int min_index, int hash_move, int ply, bool q_only = false,
                    bool q_expand = false, int out_off = 0);
    bool _end_turn_allowed(int placements_left, int placed);
    int _insert_end_turn(int ply, int n);
    int _insert_end_turn_at(int ply, int pos, int n);
    int _fill_actions(int min_index, int hash_move, int ply, int placements_left,
                      int placed, int hint_pack = 0, int hint_off = 0,
                      bool q_only = false);
    std::uint64_t _place_child_key(int cell) const;
    std::uint64_t _end_turn_child_key() const;
    bool _etc_cutoff(int depth, int beta, int placements_left, int min_index,
                     int ply, int placed, int& value, int& action);
    int _live_symmetries(int* live) const;
    bool _is_canonical_cell(int cell, const int* live, int nlive) const;
    std::vector<int> _ordered_cells(int min_index, int hash_move, int ply);

    std::uint64_t _tt_key(const GameState* state = nullptr, int min_index = 0) const;
    bool _probe(std::uint64_t key);
    // Returns true and fills outs on a hit.
    bool tt_lookup(std::uint64_t key, int& depth, int& value, int& flag,
                   std::vector<int>& turn);
    void _write_tt(std::uint64_t key, int depth, int value, int flag, int packed);
    void _write_tt(std::uint64_t key, int depth, int value, int flag,
                   const std::vector<int>& turn);
    // sym: symmetry of the node's canonical key (PV cells are stored in the
    // canonical frame so mirror images that share the key can reuse them).
    void _store(std::uint64_t key, int depth, int value, int alpha_original,
                int beta, int ply, int best_action, int sym = 0);

    void _copy_pv(int ply, int action);
    void _load_pv(int ply, int pack);
    int _negamax(int depth, int alpha, int beta, int placements_left,
                 int min_index, int ply, int hint_pack, int hint_off);
    int _pvs_place(int child_depth, int alpha, int beta, int placements_left,
                   int action, int ply, int hint_pack, int hint_off,
                   int searched, bool use_pvs_flag);
    int _pvs_end_turn(int depth, int alpha, int beta, int child_placements,
                      int ply, int searched, bool use_pvs_flag);

    using ReportFn = std::function<void(int, int, const std::vector<int>&, int, double)>;
    std::pair<std::vector<int>, SearchStats> find_turn(
        GameState& state, double time_limit, ReportFn report = nullptr);
    std::vector<int> _sanitise_turn(const GameState& state,
                                    const std::vector<int>& turn);
    int _search_root(int depth, int root_placements, int hint_pack,
                     int previous_value);
    int _seed_aspiration() const;
    int _lmr_reduction(int depth, int searched, int kind, int cell = -1) const;
    bool _tb_legal(const GameState& state) const;
    bool _tb_lookup(std::uint64_t key, int ply, int& value, int& packed) const;
    bool _singular_extend(int depth, int alpha, int beta, int action,
                          int placements_left, int ply, int hint_pack, int hint_off,
                          const int* actions, int n_actions, int index);
    void new_game();
    std::uint64_t tt_chk(std::uint64_t key) const;
    int tt_index(std::uint64_t key) const;
    bool tt_slot_live(const TTSlot& slot) const;
    void tt_lock(int index);
    void tt_unlock(int index);

private:
    void init_tt();
    void _hist_update(int& cell_hist, int bonus);
    int _history_row() const;
    void _reset_counters();
    bool _in_iid;
    bool gift_ext_used;
    // False only for the direct child of a null move, so two passes cannot stack.
    bool nmp_ok = true;
};

int pack_cells(int c0, int c1, int c2, int n);
std::vector<int> unpack_cells(int packed);
int hint_cell(int packed, int off);
int pack_to_canonical(int packed, int sym);
int pack_from_canonical(int packed, int sym);
int kind_from_masks(std::uint64_t bit, std::uint64_t merge, std::uint64_t expand,
                    std::uint64_t block, std::uint64_t harass);
int extract_bits(std::uint64_t mask, int* buf);
int classify_cell(int cell, const std::uint8_t* adj_me, const std::uint8_t* adj_opp,
                  std::uint64_t lib_me, std::uint64_t lib_opp, bool ahead);
int placement_gain(const GameState& s, int cell, int player);
int own_group_count(const GameState& s, int cell, int player);
int joined_group_size(const GameState& s, int cell, int player);
int group_nth(const GameState& s, int player, int n);

BridgeInfo own_bridges(const GameState& s, int player, int max_k);
int merge_volatility(const GameState& s);
int best_placement_gain(const GameState& s, int player);
bool exact_solve_position(const GameState& s);
// Signed RACE_WEIGHT step from the side to move. 0 when pockets overlap
// or the board is outside the race window. Not saturated.
int race_advantage(const GameState& s, int placements_left);
void insertion_sort_rank(int* buf, int start, int stop);
void insertion_sort_history(int* buf, int start, int stop, const int* hist);
void insertion_sort_swing(int* buf, int start, int stop, const int* swing,
                          const int* hist, bool use_hist);

}  // namespace catchup
