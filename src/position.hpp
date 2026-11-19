#pragma once

#include "state.hpp"

#include <cstdint>
#include <utility>
#include <vector>

namespace catchup {

// Play random legal turns. stones < 0 means fill the board.
GameState random_position(std::uint32_t seed, int stones = -1,
                          bool stop_before_end = true);

std::pair<GameState, std::vector<int>> random_playout(std::uint32_t seed);

std::vector<int> groups_by_flood_fill(const GameState& state, int player);

struct Recompute {
    std::vector<int> groups;
    int largest;
    int sumsq;
    int nstones;
    int centrality;
    int frontier;
    int adjacency[NUM_CELLS];
    int second;
    int liberties;
};

Recompute recompute(const GameState& state, int player);
std::uint64_t recompute_zobrist(const GameState& state);

}  // namespace catchup
