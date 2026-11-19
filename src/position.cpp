#include "position.hpp"

#include <algorithm>
#include <functional>
#include <random>
#include <utility>

namespace catchup {

GameState random_position(std::uint32_t seed, int stones, bool stop_before_end) {
    std::mt19937 rng(seed);
    GameState state;
    int target = stones < 0 ? NUM_CELLS : stones;
    while (!state.is_over() && (NUM_CELLS - state.empty_count) < target) {
        std::uniform_int_distribution<int> dist(1, state.max_this_turn);
        int count = dist(rng);
        std::vector<int> empty;
        empty.reserve(state.empty_count);
        for (int c = 0; c < NUM_CELLS; ++c) {
            if (!state.colour[c]) empty.push_back(c);
        }
        std::shuffle(empty.begin(), empty.end(), rng);
        if (count > static_cast<int>(empty.size())) {
            count = static_cast<int>(empty.size());
        }
        std::vector<int> turn(empty.begin(), empty.begin() + count);
        state.play_turn(turn);
        if (stop_before_end && state.is_over()) break;
    }
    return state;
}

std::pair<GameState, std::vector<int>> random_playout(std::uint32_t seed) {
    std::mt19937 rng(seed);
    GameState state;
    std::vector<int> history;
    while (!state.is_over()) {
        std::uniform_int_distribution<int> dist(1, state.max_this_turn);
        int count = dist(rng);
        std::vector<int> empty;
        for (int c = 0; c < NUM_CELLS; ++c) {
            if (!state.colour[c]) empty.push_back(c);
        }
        std::shuffle(empty.begin(), empty.end(), rng);
        if (count > static_cast<int>(empty.size())) {
            count = static_cast<int>(empty.size());
        }
        std::vector<int> turn(empty.begin(), empty.begin() + count);
        state.play_turn(turn);
        history.push_back(count);
    }
    return {std::move(state), std::move(history)};
}

std::vector<int> groups_by_flood_fill(const GameState& state, int player) {
    int stone = STONE[player];
    bool seen[NUM_CELLS] = {};
    std::vector<int> sizes;
    for (int start = 0; start < NUM_CELLS; ++start) {
        if (state.colour[start] != stone || seen[start]) continue;
        std::vector<int> stack = {start};
        seen[start] = true;
        int count = 0;
        while (!stack.empty()) {
            int cell = stack.back();
            stack.pop_back();
            count += 1;
            int ncount = NEIGHBOUR_COUNT[cell];
            for (int i = 0; i < ncount; ++i) {
                int neighbour = NEIGHBOURS[cell][i];
                if (!seen[neighbour] && state.colour[neighbour] == stone) {
                    seen[neighbour] = true;
                    stack.push_back(neighbour);
                }
            }
        }
        sizes.push_back(count);
    }
    std::sort(sizes.begin(), sizes.end(), std::greater<int>());
    return sizes;
}

static int largest_liberties_ref(const GameState& state, int player,
                                 const std::vector<int>& sizes) {
    if (sizes.empty()) return 0;
    int target = sizes[0];
    int stone = STONE[player];
    std::uint64_t seen = 0;
    for (int cell = 0; cell < NUM_CELLS; ++cell) {
        if (state.colour[cell] != stone) continue;
        int root = cell;
        while (state.parent[root] != root) root = state.parent[root];
        if (state.size[root] != target) continue;
        int ncount = NEIGHBOUR_COUNT[cell];
        for (int i = 0; i < ncount; ++i) {
            int n = NEIGHBOURS[cell][i];
            if (!state.colour[n]) seen |= (std::uint64_t{1} << n);
        }
    }
    return popcnt(seen);
}

Recompute recompute(const GameState& state, int player) {
    int stone = STONE[player];
    Recompute r;
    r.groups = groups_by_flood_fill(state, player);
    r.largest = r.groups.empty() ? 1 : r.groups[0];
    r.sumsq = 0;
    r.nstones = 0;
    for (int s : r.groups) {
        r.sumsq += s * s;
        r.nstones += s;
    }
    r.centrality = 0;
    r.frontier = 0;
    for (int c = 0; c < NUM_CELLS; ++c) {
        if (state.colour[c] == stone) {
            r.centrality += CENTRALITY[c];
            int ncount = NEIGHBOUR_COUNT[c];
            for (int i = 0; i < ncount; ++i) {
                if (!state.colour[NEIGHBOURS[c][i]]) r.frontier += 1;
            }
        }
        int adj = 0;
        int ncount = NEIGHBOUR_COUNT[c];
        for (int i = 0; i < ncount; ++i) {
            if (state.colour[NEIGHBOURS[c][i]] == stone) adj += 1;
        }
        r.adjacency[c] = adj;
    }
    r.second = r.groups.size() > 1 ? r.groups[1] : 0;
    r.liberties = largest_liberties_ref(state, player, r.groups);
    return r;
}

std::uint64_t recompute_zobrist(const GameState& state) {
    std::uint64_t key = 0;
    for (int cell = 0; cell < NUM_CELLS; ++cell) {
        int value = state.colour[cell];
        if (value) key ^= ZOBRIST_CELL[value - 1][cell];
    }
    if (state.to_move == BLACK) key ^= ZOBRIST_SIDE;
    if (state.allows_three) key ^= ZOBRIST_ALLOWS_THREE;
    return key;
}

}  // namespace catchup
