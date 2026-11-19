#pragma once

#include "search.hpp"

#include <string>
#include <utility>
#include <vector>

namespace catchup {

inline constexpr char WHITE_GLYPH = 'O';
inline constexpr char BLACK_GLYPH = 'X';

extern const char* HELP_TEXT;

std::string render(const GameState& state);
std::string describe_allowance(const GameState& state);
std::string format_status(const GameState& state);
std::string format_turn(const std::vector<int>& cells);
std::string format_result_from_sizes(const std::vector<int>& white,
                                     const std::vector<int>& black);
std::string format_result(const GameState& state);
std::vector<int> parse_turn(const std::string& text, const GameState& state);

// human_side: 0 white, 1 black, -1 self-play
int play(int human_side = WHITE, Engine* engine = nullptr,
         double total_time = 600.0, bool verbose = true);

}  // namespace catchup
