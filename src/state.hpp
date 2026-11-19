#pragma once

#include "board.hpp"

#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace catchup {

class IllegalMove : public std::runtime_error {
public:
    explicit IllegalMove(const std::string& msg) : std::runtime_error(msg) {}
};

struct BridgeInfo {
    std::uint64_t cells = 0;
    std::uint64_t starters = 0;
    int gain = 0;
};

class GameState {
public:
    std::uint8_t colour[NUM_CELLS];
    std::uint64_t bb[2];
    std::uint8_t adjacency[2][NUM_CELLS];
    std::uint64_t touch[2];
    std::uint64_t join[2];
    int empty_count;

    mutable int cache_max_r[2];
    mutable int cache_lr[2];
    mutable std::uint64_t cache_lr_mask[2];
    mutable bool cache_max_valid[2];
    mutable bool cache_lr_valid[2];
    mutable BridgeInfo cache_bridge[2];
    mutable int cache_bridge_k[2];
    mutable bool cache_bridge_valid[2];

    int parent[NUM_CELLS];
    int size[NUM_CELLS];
    int size_n[2][NUM_CELLS + 1];
    std::uint64_t lib_root[NUM_CELLS];

    int largest[2];
    int second[2];
    int largest_root[2];
    int sumsq[2];
    int nstones[2];
    int centrality[2];
    int frontier[2];

    int to_move;
    int turn_index;
    int placed_this_turn;
    int max_this_turn;
    int score_at_turn_start;
    bool score_increased[2];
    bool allows_three;

    std::uint64_t zobrist;

    int _undo_n;
    int _turn_undo_n;
    int _lib_undo_n;

    int _u_cell[256];
    int _u_largest[256];
    int _u_second[256];
    int _u_largest_root[256];
    int _u_sumsq[256];
    int _u_front_own[256];
    int _u_front_opp[256];
    int _u_m0[256];
    int _u_m1[256];
    int _u_m2[256];
    int _u_lib_mark[256];
    int _t_inc[256];
    int _t_start[256];
    int _t_max[256];
    int _t_placed[256];
    int _t_three[256];
    std::vector<int> _lib_idx;
    std::vector<std::uint64_t> _lib_old;

    GameState();
    GameState(const GameState& other);
    GameState& operator=(const GameState& other);
    GameState(GameState&&) noexcept = default;
    GameState& operator=(GameState&&) noexcept = default;

    int find(int cell) const;
    std::uint64_t largest_lib_mask(int player) const;
    int largest_liberties(int player) const;
    int max_reachable(int player) const;
    int largest_reachable(int player) const;
    // (own stones ∪ empty) component of the current largest group.
    // Same flood as largest_reachable; the mask is cached with the size.
    std::uint64_t largest_region(int player) const;
    void invalidate_reach_cache();
    void invalidate_bridge_cache();

    void place(int cell);
    void unplace();
    void end_turn();
    void unend_turn();

    bool is_over() const { return empty_count == 0; }
    int placements_left() const { return max_this_turn - placed_this_turn; }
    int score(int player) const { return largest[player]; }
    std::vector<int> group_sizes(int player) const;
    int compare_final() const;
    int terminal_score(int player) const;
    std::vector<int> legal_turn_sizes() const;

    void play_turn(const std::vector<int>& cells);
    void undo_turn(int stone_count);
    GameState clone() const;
    // Throws if derived fields disagree with a full recompute.
    void sanity_check() const;

private:
    void reset_undo_storage();
    void copy_position_from(const GameState& other);
    void lib_write(int idx, std::uint64_t value);
    void pull_top2(int player, int cap);
    int compute_max_reachable(int player) const;
    int compute_largest_reachable(int player) const;
};

// Returns the smallest occupancy Zobrist key in the D6 orbit (identity frame
// only when min_index > 0, see state.cpp), combined with the turn metadata
// that distinguishes search subpositions.
std::uint64_t get_canonical_key(const GameState& state, int min_index = 0);
// Same key; *sym_out receives the symmetry g whose image SYM.perm[g] gives the
// canonical board (0 = identity). Cells stored with a canonical key are kept in
// that canonical frame: map a cell c to it with SYM.perm[g][c] and back with
// sym_inverse_cell(g, c).
std::uint64_t get_canonical_key(const GameState& state, int min_index, int* sym_out);
int sym_inverse_cell(int sym, int cell);

}  // namespace catchup
