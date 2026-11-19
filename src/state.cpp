#include "state.hpp"
#include "position.hpp"

#include <algorithm>
#include <cstring>
#include <functional>
#include <stdexcept>

namespace catchup {

std::uint64_t get_canonical_key(const GameState& state, int min_index) {
    return get_canonical_key(state, min_index, nullptr);
}

std::uint64_t get_canonical_key(const GameState& state, int min_index, int* sym_out) {
    if (min_index < 0 || min_index > NUM_CELLS) {
        throw std::out_of_range("min_index outside the board");
    }

    // Inside a turn (min_index > 0) the remaining placements are restricted to
    // cells with index >= min_index. That restriction is tied to the board's
    // own coordinates and is not preserved by the D6 symmetries, so mirror
    // images are not equivalent search nodes there: use the identity frame
    // only. At min_index == 0 every empty cell is allowed and the symmetry-
    // canonical board key is sound.
    const int n_symmetries = (min_index > 0) ? 1 : N_SYMMETRIES;
    std::uint64_t best_board = UINT64_MAX;
    int best_symmetry = 0;
    for (int symmetry = 0; symmetry < n_symmetries; ++symmetry) {
        std::uint64_t key = 0;
        for (int player = 0; player < 2; ++player) {
            std::uint64_t stones = state.bb[player];
            while (stones) {
                std::uint64_t bit = stones & -stones;
                int cell = ctz(bit);
                stones ^= bit;
                key ^= ZOBRIST_CELL[player][SYM.perm[symmetry][cell]];
            }
        }
        if (key < best_board) {
            best_board = key;
            best_symmetry = symmetry;
        }
    }
    if (sym_out) *sym_out = best_symmetry;
    std::uint64_t key = best_board;
    if (state.to_move == BLACK) key ^= ZOBRIST_SIDE;
    if (state.allows_three) key ^= ZOBRIST_ALLOWS_THREE;
    key ^= ZOBRIST_PLACED[state.placed_this_turn];
    key ^= ZOBRIST_SCORE_START[state.score_at_turn_start];
    key ^= ZOBRIST_MIN_INDEX[min_index];
    return key;
}

namespace {
struct SymInverse {
    int inv[N_SYMMETRIES][NUM_CELLS];
    SymInverse() {
        for (int g = 0; g < N_SYMMETRIES; ++g)
            for (int c = 0; c < NUM_CELLS; ++c) inv[g][SYM.perm[g][c]] = c;
    }
};
const SymInverse SYM_INV;
}  // namespace

int sym_inverse_cell(int sym, int cell) { return SYM_INV.inv[sym][cell]; }

GameState::GameState() {
    std::memset(colour, 0, sizeof(colour));
    bb[0] = bb[1] = 0;
    std::memset(adjacency, 0, sizeof(adjacency));
    touch[0] = touch[1] = 0;
    join[0] = join[1] = 0;
    empty_count = NUM_CELLS;

    for (int i = 0; i < NUM_CELLS; ++i) {
        parent[i] = i;
        size[i] = 0;
        lib_root[i] = 0;
    }
    std::memset(size_n, 0, sizeof(size_n));

    largest[0] = largest[1] = 1;
    second[0] = second[1] = 0;
    largest_root[0] = largest_root[1] = -1;
    sumsq[0] = sumsq[1] = 0;
    nstones[0] = nstones[1] = 0;
    centrality[0] = centrality[1] = 0;
    frontier[0] = frontier[1] = 0;

    to_move = WHITE;
    turn_index = 0;
    placed_this_turn = 0;
    max_this_turn = 1;
    score_at_turn_start = 1;
    score_increased[0] = score_increased[1] = false;
    allows_three = false;
    zobrist = 0;
    invalidate_reach_cache();
    reset_undo_storage();
}

void GameState::reset_undo_storage() {
    _undo_n = 0;
    _turn_undo_n = 0;
    _lib_undo_n = 0;
    _lib_idx.assign(2048, 0);
    _lib_old.assign(2048, 0);
}

void GameState::copy_position_from(const GameState& other) {
    std::memcpy(colour, other.colour, sizeof(colour));
    bb[0] = other.bb[0];
    bb[1] = other.bb[1];
    std::memcpy(adjacency, other.adjacency, sizeof(adjacency));
    touch[0] = other.touch[0];
    touch[1] = other.touch[1];
    join[0] = other.join[0];
    join[1] = other.join[1];
    empty_count = other.empty_count;
    std::memcpy(parent, other.parent, sizeof(parent));
    std::memcpy(size, other.size, sizeof(size));
    std::memcpy(size_n, other.size_n, sizeof(size_n));
    std::memcpy(lib_root, other.lib_root, sizeof(lib_root));
    largest[0] = other.largest[0];
    largest[1] = other.largest[1];
    second[0] = other.second[0];
    second[1] = other.second[1];
    largest_root[0] = other.largest_root[0];
    largest_root[1] = other.largest_root[1];
    sumsq[0] = other.sumsq[0];
    sumsq[1] = other.sumsq[1];
    nstones[0] = other.nstones[0];
    nstones[1] = other.nstones[1];
    centrality[0] = other.centrality[0];
    centrality[1] = other.centrality[1];
    frontier[0] = other.frontier[0];
    frontier[1] = other.frontier[1];
    to_move = other.to_move;
    turn_index = other.turn_index;
    placed_this_turn = other.placed_this_turn;
    max_this_turn = other.max_this_turn;
    score_at_turn_start = other.score_at_turn_start;
    score_increased[0] = other.score_increased[0];
    score_increased[1] = other.score_increased[1];
    allows_three = other.allows_three;
    zobrist = other.zobrist;
    cache_max_r[0] = other.cache_max_r[0];
    cache_max_r[1] = other.cache_max_r[1];
    cache_lr[0] = other.cache_lr[0];
    cache_lr[1] = other.cache_lr[1];
    cache_lr_mask[0] = other.cache_lr_mask[0];
    cache_lr_mask[1] = other.cache_lr_mask[1];
    cache_max_valid[0] = other.cache_max_valid[0];
    cache_max_valid[1] = other.cache_max_valid[1];
    cache_lr_valid[0] = other.cache_lr_valid[0];
    cache_lr_valid[1] = other.cache_lr_valid[1];
    cache_bridge[0] = other.cache_bridge[0];
    cache_bridge[1] = other.cache_bridge[1];
    cache_bridge_k[0] = other.cache_bridge_k[0];
    cache_bridge_k[1] = other.cache_bridge_k[1];
    cache_bridge_valid[0] = other.cache_bridge_valid[0];
    cache_bridge_valid[1] = other.cache_bridge_valid[1];
}

GameState::GameState(const GameState& other) {
    copy_position_from(other);
    reset_undo_storage();
}

GameState& GameState::operator=(const GameState& other) {
    if (this != &other) {
        copy_position_from(other);
        reset_undo_storage();
    }
    return *this;
}

int GameState::find(int cell) const {
    while (parent[cell] != cell) {
        cell = parent[cell];
    }
    return cell;
}

void GameState::lib_write(int idx, std::uint64_t value) {
    int n = _lib_undo_n;
    if (n >= static_cast<int>(_lib_idx.size())) {
        _lib_idx.resize(n + 256, 0);
        _lib_old.resize(n + 256, 0);
    }
    _lib_idx[n] = idx;
    _lib_old[n] = lib_root[idx];
    _lib_undo_n = n + 1;
    lib_root[idx] = value;
}

void GameState::pull_top2(int player, int cap) {
    const int* n = size_n[player];
    int lg = 0;
    int sec = 0;
    for (int s = cap; s > 0; --s) {
        int c = n[s];
        if (!c) continue;
        if (lg == 0) {
            lg = s;
            if (c >= 2) {
                sec = s;
                break;
            }
        } else {
            sec = s;
            break;
        }
    }
    largest[player] = lg ? lg : 1;
    second[player] = sec;
}

std::uint64_t GameState::largest_lib_mask(int player) const {
    if (nstones[player] == 0) return 0;
    int root = largest_root[player];
    if (root >= 0) return lib_root[root];
    int target = largest[player];
    std::uint64_t bits = bb[player];
    std::uint64_t mask = 0;
    while (bits) {
        std::uint64_t lsb = bits & -bits;
        int cell = ctz(lsb);
        bits ^= lsb;
        if (parent[cell] == cell && size[cell] == target) {
            mask |= lib_root[cell];
        }
    }
    return mask;
}

int GameState::largest_liberties(int player) const {
    return popcnt(largest_lib_mask(player));
}

static std::uint64_t flood_component(std::uint64_t walk, std::uint64_t seed) {
    std::uint64_t comp = seed;
    std::uint64_t frontier = seed;
    while (frontier) {
        std::uint64_t grow = 0;
        std::uint64_t f = frontier;
        while (f) {
            std::uint64_t bit = f & -f;
            grow |= NEIGHBOUR_MASK[ctz(bit)];
            f ^= bit;
        }
        frontier = grow & walk & ~comp;
        comp |= frontier;
    }
    return comp;
}

void GameState::invalidate_bridge_cache() {
    cache_bridge_valid[0] = cache_bridge_valid[1] = false;
}

void GameState::invalidate_reach_cache() {
    cache_max_valid[0] = cache_max_valid[1] = false;
    cache_lr_valid[0] = cache_lr_valid[1] = false;
    cache_lr_mask[0] = cache_lr_mask[1] = 0;
    invalidate_bridge_cache();
}

int GameState::max_reachable(int player) const {
    if (cache_max_valid[player]) return cache_max_r[player];
    cache_max_r[player] = compute_max_reachable(player);
    cache_max_valid[player] = true;
    return cache_max_r[player];
}

int GameState::largest_reachable(int player) const {
    if (cache_lr_valid[player]) return cache_lr[player];
    cache_lr[player] = compute_largest_reachable(player);
    cache_lr_valid[player] = true;
    return cache_lr[player];
}

std::uint64_t GameState::largest_region(int player) const {
    (void)largest_reachable(player);
    return cache_lr_mask[player];
}

int GameState::compute_max_reachable(int player) const {
    // Largest connected component of this player's stones plus every empty
    // cell. Opponent stones are walls, so this is the most that player can
    // ever form if they were given every remaining empty.
    std::uint64_t walk = (bb[player] | (FULL_MASK ^ (bb[0] | bb[1]))) & FULL_MASK;
    int best = 0;
    std::uint64_t remaining = walk;
    while (remaining) {
        std::uint64_t seed = remaining & -remaining;
        std::uint64_t comp = flood_component(walk, seed);
        int sz = popcnt(comp);
        if (sz > best) best = sz;
        remaining &= ~comp;
    }
    return best;
}

int GameState::compute_largest_reachable(int player) const {
    // Size of the (own stones ∪ empty) component that contains this player's
    // current largest group. One seeded flood, not a walk of every region.
    // The mask is kept so a race can count empty holes without a second flood.
    std::uint64_t walk = (bb[player] | (FULL_MASK ^ (bb[0] | bb[1]))) & FULL_MASK;
    if (nstones[player] == 0) {
        cache_lr_mask[player] = walk;
        return popcnt(walk);
    }
    int seed_cell = largest_root[player];
    if (seed_cell < 0) {
        int target = largest[player];
        std::uint64_t bits = bb[player];
        seed_cell = -1;
        while (bits) {
            std::uint64_t lsb = bits & -bits;
            int cell = ctz(lsb);
            bits ^= lsb;
            if (parent[cell] == cell && size[cell] == target) {
                seed_cell = cell;
                break;
            }
        }
        if (seed_cell < 0) {
            cache_lr_mask[player] = walk;
            return popcnt(walk);
        }
    }
    std::uint64_t comp = flood_component(walk, CELL_BIT[seed_cell]);
    cache_lr_mask[player] = comp;
    return popcnt(comp);
}

void GameState::place(int cell) {
    int player = to_move;
    int opponent = 1 - player;
    int stone = player + 1;
    int lib_mark = _lib_undo_n;

    int old_largest = largest[player];
    int old_second = second[player];
    int old_largest_root = largest_root[player];
    int old_sumsq = sumsq[player];
    int own_frontier = frontier[player];
    int opp_frontier = frontier[opponent];
    int old_frontier_own = own_frontier;
    int old_frontier_opp = opp_frontier;

    colour[cell] = static_cast<std::uint8_t>(stone);
    bb[player] |= (std::uint64_t{1} << cell);
    empty_count -= 1;
    nstones[player] += 1;
    centrality[player] += CENTRALITY[cell];
    zobrist ^= ZOBRIST_CELL[player][cell];

    std::uint64_t empty_bits = 0;
    int ncount = NEIGHBOUR_COUNT[cell];
    for (int i = 0; i < ncount; ++i) {
        int n = NEIGHBOURS[cell][i];
        if (!colour[n]) empty_bits |= (std::uint64_t{1} << n);
    }
    lib_write(cell, empty_bits);

    int* size_hist = size_n[player];
    std::uint8_t* own_adjacency = adjacency[player];
    std::uint64_t touch_bits = touch[player];
    std::uint64_t join_bits = join[player];
    parent[cell] = cell;
    size[cell] = 1;
    size_hist[1] += 1;
    int sumsq_v = old_sumsq + 1;
    int root = cell;
    int m0 = -1, m1 = -1, m2 = -1;
    int merged = 0;

    for (int i = 0; i < ncount; ++i) {
        int n = NEIGHBOURS[cell][i];
        int adj = own_adjacency[n];
        own_adjacency[n] = static_cast<std::uint8_t>(adj + 1);
        if (adj == 0) {
            touch_bits |= (std::uint64_t{1} << n);
        } else if (adj == 1) {
            join_bits |= (std::uint64_t{1} << n);
        }
        int neighbour_colour = colour[n];
        if (!neighbour_colour) {
            own_frontier += 1;
        } else if (neighbour_colour == stone) {
            own_frontier -= 1;
            int other = n;
            while (parent[other] != other) other = parent[other];
            if (other != root) {
                if (size[other] > size[root]) std::swap(root, other);
                int sa = size[root];
                int sb = size[other];
                size_hist[sa] -= 1;
                size_hist[sb] -= 1;
                int combined = sa + sb;
                size_hist[combined] += 1;
                sumsq_v += combined * combined - sa * sa - sb * sb;
                parent[other] = root;
                size[root] = combined;
                lib_write(root, (lib_root[root] | lib_root[other]) & ~(std::uint64_t{1} << cell));
                lib_write(other, 0);
                if (merged == 0) m0 = other;
                else if (merged == 1) m1 = other;
                else m2 = other;
                merged += 1;
            }
        } else {
            opp_frontier -= 1;
            int opp_root = n;
            while (parent[opp_root] != opp_root) opp_root = parent[opp_root];
            lib_write(opp_root, lib_root[opp_root] & ~(std::uint64_t{1} << cell));
        }
    }

    touch[player] = touch_bits;
    join[player] = join_bits;
    frontier[player] = own_frontier;
    frontier[opponent] = opp_frontier;
    sumsq[player] = sumsq_v;
    int cap = size[root];
    if (old_largest > cap) cap = old_largest;
    pull_top2(player, cap);
    int lg = largest[player];
    if (size_hist[lg] == 1) {
        largest_root[player] = (size[root] == lg) ? root : old_largest_root;
    } else {
        largest_root[player] = -1;
    }

    placed_this_turn += 1;
    int un = _undo_n;
    if (un >= 256) {
        throw std::runtime_error("place undo stack overflow");
    }
    _u_cell[un] = cell;
    _u_largest[un] = old_largest;
    _u_second[un] = old_second;
    _u_largest_root[un] = old_largest_root;
    _u_sumsq[un] = old_sumsq;
    _u_front_own[un] = old_frontier_own;
    _u_front_opp[un] = old_frontier_opp;
    _u_m0[un] = m0;
    _u_m1[un] = m1;
    _u_m2[un] = m2;
    _u_lib_mark[un] = lib_mark;
    _undo_n = un + 1;
    invalidate_reach_cache();
#ifndef NDEBUG
    sanity_check();
#endif
}

void GameState::unplace() {
    int n = _undo_n - 1;
    _undo_n = n;
    int cell = _u_cell[n];
    int old_largest = _u_largest[n];
    int old_second = _u_second[n];
    int old_largest_root = _u_largest_root[n];
    int old_sumsq = _u_sumsq[n];
    int old_frontier_own = _u_front_own[n];
    int old_frontier_opp = _u_front_opp[n];
    int m0 = _u_m0[n];
    int m1 = _u_m1[n];
    int m2 = _u_m2[n];
    int lib_mark = _u_lib_mark[n];

    int player = colour[cell] - 1;
    int* size_hist = size_n[player];
    int merges[3] = {m2, m1, m0};
    for (int other : merges) {
        if (other >= 0) {
            int combined = size[parent[other]];
            int sb = size[other];
            int sa = combined - sb;
            size_hist[combined] -= 1;
            size_hist[sa] += 1;
            size_hist[sb] += 1;
            size[parent[other]] -= size[other];
            parent[other] = other;
        }
    }

    size_hist[1] -= 1;
    size[cell] = 0;
    parent[cell] = cell;

    colour[cell] = EMPTY;
    bb[player] &= ~(std::uint64_t{1} << cell);
    std::uint8_t* own_adjacency = adjacency[player];
    std::uint64_t touch_bits = touch[player];
    std::uint64_t join_bits = join[player];
    int ncount = NEIGHBOUR_COUNT[cell];
    for (int i = 0; i < ncount; ++i) {
        int nb = NEIGHBOURS[cell][i];
        int adj = own_adjacency[nb];
        own_adjacency[nb] = static_cast<std::uint8_t>(adj - 1);
        if (adj == 1) {
            touch_bits &= ~(std::uint64_t{1} << nb);
        } else if (adj == 2) {
            join_bits &= ~(std::uint64_t{1} << nb);
        }
    }
    touch[player] = touch_bits;
    join[player] = join_bits;

    empty_count += 1;
    nstones[player] -= 1;
    centrality[player] -= CENTRALITY[cell];
    zobrist ^= ZOBRIST_CELL[player][cell];

    largest[player] = old_largest;
    second[player] = old_second;
    largest_root[player] = old_largest_root;
    sumsq[player] = old_sumsq;
    frontier[player] = old_frontier_own;
    frontier[1 - player] = old_frontier_opp;

    while (_lib_undo_n > lib_mark) {
        _lib_undo_n -= 1;
        int k = _lib_undo_n;
        lib_root[_lib_idx[k]] = _lib_old[k];
    }

    placed_this_turn -= 1;
    invalidate_reach_cache();
#ifndef NDEBUG
    sanity_check();
#endif
}

void GameState::end_turn() {
    int player = to_move;
    int opponent = 1 - player;

    int n = _turn_undo_n;
    if (n >= 256) {
        throw std::runtime_error("turn undo stack overflow");
    }
    _t_inc[n] = score_increased[player] ? 1 : 0;
    _t_start[n] = score_at_turn_start;
    _t_max[n] = max_this_turn;
    _t_placed[n] = placed_this_turn;
    _t_three[n] = allows_three ? 1 : 0;
    _turn_undo_n = n + 1;

    bool increased = largest[player] > score_at_turn_start;
    score_increased[player] = increased;

    to_move = opponent;
    turn_index += 1;
    placed_this_turn = 0;
    score_at_turn_start = largest[opponent];

    bool next_three = increased && largest[player] >= largest[opponent];
    max_this_turn = std::min(next_three ? 3 : 2, empty_count);

    zobrist ^= ZOBRIST_SIDE;
    if (next_three != allows_three) {
        allows_three = next_three;
        zobrist ^= ZOBRIST_ALLOWS_THREE;
    }
    invalidate_bridge_cache();
#ifndef NDEBUG
    sanity_check();
#endif
}

void GameState::unend_turn() {
    int n = _turn_undo_n - 1;
    _turn_undo_n = n;
    int score_inc = _t_inc[n];
    int start = _t_start[n];
    int max_turn = _t_max[n];
    int placed = _t_placed[n];
    bool was_three = _t_three[n] != 0;

    int opponent = to_move;
    int player = 1 - opponent;

    to_move = player;
    turn_index -= 1;
    score_increased[player] = score_inc != 0;
    score_at_turn_start = start;
    max_this_turn = max_turn;
    placed_this_turn = placed;

    zobrist ^= ZOBRIST_SIDE;
    if (was_three != allows_three) {
        allows_three = was_three;
        zobrist ^= ZOBRIST_ALLOWS_THREE;
    }
    invalidate_bridge_cache();
#ifndef NDEBUG
    sanity_check();
#endif
}

void GameState::sanity_check() const {
    if (empty_count != NUM_CELLS - nstones[WHITE] - nstones[BLACK]) {
        throw std::logic_error("sanity: empty_count");
    }
    if (zobrist != recompute_zobrist(*this)) {
        throw std::logic_error("sanity: zobrist");
    }
    for (int player = 0; player < 2; ++player) {
        Recompute expected = recompute(*this, player);
        if (largest[player] != expected.largest) throw std::logic_error("sanity: largest");
        if (second[player] != expected.second) throw std::logic_error("sanity: second");
        if (sumsq[player] != expected.sumsq) throw std::logic_error("sanity: sumsq");
        if (nstones[player] != expected.nstones) throw std::logic_error("sanity: nstones");
        if (centrality[player] != expected.centrality) throw std::logic_error("sanity: centrality");
        if (frontier[player] != expected.frontier) throw std::logic_error("sanity: frontier");
        if (largest_liberties(player) != expected.liberties) {
            throw std::logic_error("sanity: liberties");
        }
        if (group_sizes(player) != expected.groups) throw std::logic_error("sanity: groups");
        int hist[NUM_CELLS + 1] = {};
        for (int g : expected.groups) {
            if (g < 0 || g > NUM_CELLS) throw std::logic_error("sanity: group size");
            hist[g] += 1;
        }
        for (int s = 0; s <= NUM_CELLS; ++s) {
            if (size_n[player][s] != hist[s]) throw std::logic_error("sanity: size_n");
        }
        std::uint64_t bits = 0;
        std::uint64_t touch_bb = 0;
        std::uint64_t join_bb = 0;
        int stone = STONE[player];
        for (int c = 0; c < NUM_CELLS; ++c) {
            if (static_cast<int>(adjacency[player][c]) != expected.adjacency[c]) {
                throw std::logic_error("sanity: adjacency");
            }
            if (colour[c] == stone) bits |= std::uint64_t{1} << c;
            if (adjacency[player][c]) {
                touch_bb |= std::uint64_t{1} << c;
                if (adjacency[player][c] >= 2) join_bb |= std::uint64_t{1} << c;
            }
        }
        if (bb[player] != bits) throw std::logic_error("sanity: bb");
        if (touch[player] != touch_bb) throw std::logic_error("sanity: touch");
        if (join[player] != join_bb) throw std::logic_error("sanity: join");

        int lg = largest[player];
        int root = largest_root[player];
        bool unique = nstones[player] > 0 && lg >= 1 && lg <= NUM_CELLS
                      && size_n[player][lg] == 1;
        if (!unique) {
            if (root != -1) throw std::logic_error("sanity: largest_root");
        } else if (root < 0 || root >= NUM_CELLS || parent[root] != root
                   || colour[root] != stone || size[root] != lg) {
            throw std::logic_error("sanity: largest_root");
        }
    }
}

std::vector<int> GameState::group_sizes(int player) const {
    int stone = STONE[player];
    std::vector<int> sizes;
    for (int c = 0; c < NUM_CELLS; ++c) {
        if (colour[c] == stone && parent[c] == c) {
            sizes.push_back(size[c]);
        }
    }
    std::sort(sizes.begin(), sizes.end(), std::greater<int>());
    return sizes;
}

int GameState::compare_final() const {
    const int* nw = size_n[WHITE];
    const int* nb = size_n[BLACK];
    int s = NUM_CELLS;
    while (s > 0) {
        int cw = nw[s];
        int cb = nb[s];
        if (cw != cb) return cw > cb ? 1 : -1;
        --s;
    }
    return 0;
}

int GameState::terminal_score(int player) const {
    int result = compare_final();
    if (result == 0) return 0;
    int winner = result == 1 ? WHITE : BLACK;
    return winner == player ? WIN_SCORE : -WIN_SCORE;
}

std::vector<int> GameState::legal_turn_sizes() const {
    std::vector<int> sizes;
    for (int i = 1; i <= max_this_turn; ++i) sizes.push_back(i);
    return sizes;
}

void GameState::play_turn(const std::vector<int>& cells) {
    if (cells.empty()) {
        throw IllegalMove("a turn must place at least one stone");
    }
    if (static_cast<int>(cells.size()) > max_this_turn) {
        throw IllegalMove("only " + std::to_string(max_this_turn) +
                          " stone(s) may be placed this turn");
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        for (std::size_t j = i + 1; j < cells.size(); ++j) {
            if (cells[i] == cells[j]) {
                throw IllegalMove("cannot place two stones on the same cell");
            }
        }
    }
    for (int cell : cells) {
        if (cell < 0 || cell >= NUM_CELLS) {
            throw IllegalMove("cell index " + std::to_string(cell) + " is off the board");
        }
        if (colour[cell] != EMPTY) {
            throw IllegalMove(std::string(CELL_NAMES[cell]) + " is already occupied");
        }
    }
    for (int cell : cells) place(cell);
    end_turn();
}

void GameState::undo_turn(int stone_count) {
    unend_turn();
    for (int i = 0; i < stone_count; ++i) unplace();
}

GameState GameState::clone() const {
    return GameState(*this);
}

}  // namespace catchup
