#include "ui.hpp"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <sstream>

namespace catchup {

const char* HELP_TEXT =
    "Commands\n"
    "  <cells>   place stones, e.g.  e5   or   e5 d4   or   e5 d4 c3\n"
    "  hint      let the engine suggest a turn for you\n"
    "  board     redraw the board\n"
    "  undo      take back your last turn and the engine's reply\n"
    "  help      show this text\n"
    "  quit      leave the game\n";

std::string render(const GameState& state) {
    std::ostringstream lines;
    for (int r = 0; r < GRID; ++r) {
        const DisplayRow& row = DISPLAY_ROWS[r];
        if (r) lines << "\n";
        lines << "  " << row.number << "  " << std::string(row.indent, ' ');
        for (int i = 0; i < row.ncells; ++i) {
            if (i) lines << " ";
            int cell = row.cells[i];
            int value = state.colour[cell];
            if (value == 0) {
                lines << static_cast<char>(std::tolower(static_cast<unsigned char>(LETTERS[CELL_X[cell]])));
            } else if (value == 1) {
                lines << WHITE_GLYPH;
            } else {
                lines << BLACK_GLYPH;
            }
        }
    }
    return lines.str();
}

std::string describe_allowance(const GameState& state) {
    int mover = state.to_move;
    int opponent = 1 - mover;
    int allowed = state.max_this_turn;

    if (state.turn_index == 0) {
        return "1 stone (White's opening move is always a single stone)";
    }

    std::string reason;
    if (state.allows_three) {
        reason = std::string(COLOUR_NAMES[opponent]) + "'s score rose to "
                 + std::to_string(state.score(opponent))
                 + " and is at least your " + std::to_string(state.score(mover));
    } else if (!state.score_increased[opponent]) {
        reason = std::string(COLOUR_NAMES[opponent]) + "'s score did not rise last turn";
    } else {
        reason = std::string(COLOUR_NAMES[opponent]) + "'s score of "
                 + std::to_string(state.score(opponent)) + " is below your "
                 + std::to_string(state.score(mover));
    }

    std::string text = "1 to " + std::to_string(allowed) + " stone"
                       + (allowed == 1 ? "" : "s") + " (" + reason + ")";
    if (allowed < (state.allows_three ? 3 : 2)) {
        text += ", capped by the " + std::to_string(state.empty_count) + " empty cell(s) left";
    }
    return text;
}

std::string format_status(const GameState& state) {
    std::ostringstream lines;
    lines << "  White " << WHITE_GLYPH << " : score " << state.score(WHITE)
          << "      Black " << BLACK_GLYPH << " : score " << state.score(BLACK)
          << "      " << state.empty_count << " empty";
    if (!state.is_over()) {
        lines << "\n  " << COLOUR_NAMES[state.to_move] << " to move, may place "
              << describe_allowance(state);
    }
    return lines.str();
}

std::string format_turn(const std::vector<int>& cells) {
    std::ostringstream oss;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (i) oss << " ";
        oss << CELL_NAMES[cells[i]];
    }
    return oss.str();
}

std::string format_result_from_sizes(const std::vector<int>& white,
                                     const std::vector<int>& black) {
    auto join = [](const std::vector<int>& v) {
        std::ostringstream oss;
        for (std::size_t i = 0; i < v.size(); ++i) {
            if (i) oss << ", ";
            oss << v[i];
        }
        return oss.str();
    };

    int outcome = 0;
    int n = static_cast<int>(std::max(white.size(), black.size()));
    for (int i = 0; i < n; ++i) {
        int w = i < static_cast<int>(white.size()) ? white[i] : 0;
        int b = i < static_cast<int>(black.size()) ? black[i] : 0;
        if (w != b) {
            outcome = w > b ? 1 : -1;
            break;
        }
    }

    std::ostringstream lines;
    lines << "  White groups: " << join(white) << "\n";
    lines << "  Black groups: " << join(black);
    if (outcome == 0) {
        lines << "\n  Draw: every group size matches.";
    } else {
        int winner = outcome == 1 ? WHITE : BLACK;
        for (int i = 0; i < n; ++i) {
            int w = i < static_cast<int>(white.size()) ? white[i] : 0;
            int b = i < static_cast<int>(black.size()) ? black[i] : 0;
            if (w != b) {
                std::string where;
                if (i == 0) where = "largest";
                else if (i == 1) where = "second largest";
                else if (i == 2) where = "third largest";
                else where = "group number " + std::to_string(i + 1);
                lines << "\n  " << COLOUR_NAMES[winner] << " wins: " << where
                      << " group " << std::max(w, b) << " beats " << std::min(w, b) << ".";
                break;
            }
        }
    }
    return lines.str();
}

std::string format_result(const GameState& state) {
    return format_result_from_sizes(state.group_sizes(WHITE), state.group_sizes(BLACK));
}

std::vector<int> parse_turn(const std::string& text, const GameState& state) {
    std::string normalised = text;
    for (char& c : normalised) {
        if (c == ',') c = ' ';
    }
    std::istringstream iss(normalised);
    std::string token;
    std::vector<std::string> tokens;
    while (iss >> token) tokens.push_back(token);
    if (tokens.empty()) {
        throw std::invalid_argument("type at least one cell, for example E5");
    }

    std::vector<int> cells;
    for (const std::string& tok : tokens) {
        std::string key;
        for (char c : tok) {
            key.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
        }
        int cell = name_to_index(key.c_str());
        if (cell < 0) {
            throw std::invalid_argument("'" + tok + "' is not a cell on this board");
        }
        if (state.colour[cell]) {
            throw std::invalid_argument(std::string(CELL_NAMES[cell]) + " is already occupied");
        }
        for (int prev : cells) {
            if (prev == cell) {
                throw std::invalid_argument(std::string(CELL_NAMES[cell]) + " is listed twice");
            }
        }
        cells.push_back(cell);
    }
    if (static_cast<int>(cells.size()) > state.max_this_turn) {
        throw std::invalid_argument(
            "you may place at most " + std::to_string(state.max_this_turn)
            + " stone(s) this turn, not " + std::to_string(cells.size()));
    }
    return cells;
}

namespace {

using clock = std::chrono::steady_clock;

double now() {
    return std::chrono::duration<double>(clock::now().time_since_epoch()).count();
}

// Returns: 0 = cells in out, 1 = undo, 2 = quit
int prompt_human(GameState& state, Engine& engine, Clock& clk,
                 std::vector<int>& out) {
    while (true) {
        std::cout << "  " << COLOUR_NAMES[state.to_move] << " > " << std::flush;
        std::string text;
        if (!std::getline(std::cin, text)) return 2;
        // trim
        auto b = text.find_first_not_of(" \t");
        if (b == std::string::npos) continue;
        auto e = text.find_last_not_of(" \t");
        text = text.substr(b, e - b + 1);

        std::string command = text;
        for (char& c : command) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (command == "quit" || command == "exit" || command == "q") return 2;
        if (command == "help" || command == "?") {
            std::cout << HELP_TEXT;
            continue;
        }
        if (command == "board") {
            std::cout << render(state) << "\n" << format_status(state) << "\n";
            continue;
        }
        if (command == "undo") return 1;
        if (command == "hint") {
            double budget = std::min(5.0, clk.allocate(state));
            GameState snap = state.clone();
            auto result = engine.find_turn(snap, budget);
            std::cout << "  hint: " << format_turn(result.first) << "   ("
                      << result.second.describe() << ")\n";
            continue;
        }
        try {
            out = parse_turn(text, state);
            return 0;
        } catch (const std::invalid_argument& exc) {
            std::cout << "  " << exc.what() << ".  Type 'help' for the commands.\n";
        }
    }
}

}  // namespace

int play(int human_side, Engine* engine_ptr, double total_time, bool verbose) {
    GameState state;
    Engine local;
    Engine& engine = engine_ptr ? *engine_ptr : local;
    engine.new_game();
    Clock clocks[2] = {Clock(total_time), Clock(total_time)};
    struct TurnRecord {
        int stones;
        int mover;
        double used_before;
    };
    std::vector<TurnRecord> history;

    std::cout << HELP_TEXT;
    while (!state.is_over()) {
        std::cout << "\n" << render(state) << "\n" << format_status(state) << "\n";
        int mover = state.to_move;
        double used_before = clocks[mover].used;
        std::vector<int> cells;
        if (human_side >= 0 && mover == human_side) {
            int kind = prompt_human(state, engine, clocks[mover], cells);
            if (kind == 2) {
                std::cout << "\n  Game abandoned.\n";
                return -2;
            }
            if (kind == 1) {
                if (history.size() < 2) {
                    std::cout << "  Nothing to undo yet.\n";
                    continue;
                }
                for (int i = 0; i < 2; ++i) {
                    TurnRecord rec = history.back();
                    history.pop_back();
                    clocks[rec.mover].used = rec.used_before;
                    state.undo_turn(rec.stones);
                    clocks[rec.mover].undo_note();
                    clocks[1 - rec.mover].undo_note();
                }
                continue;
            }
        } else {
            double budget = clocks[mover].allocate(state);
            double started = now();
            auto result = engine.find_turn(state, budget);
            cells = result.first;
            clocks[mover].consume(now() - started);
            if (verbose) {
                std::cout << "  " << COLOUR_NAMES[mover] << " plays "
                          << format_turn(cells) << "\n";
                char left[32];
                std::snprintf(left, sizeof(left), "%.0f", clocks[mover].remaining());
                std::cout << "  " << result.second.describe() << " | " << left
                          << "s of budget left\n";
            }
        }
        int empty_before = state.empty_count;
        try {
            state.play_turn(cells);
        } catch (const IllegalMove& exc) {
            std::cout << "  Illegal: " << exc.what() << "\n";
            continue;
        }
        int stones = empty_before - state.empty_count;
        if (stones < 0) stones = 0;
        clocks[mover].note_own_stones(stones);
        clocks[1 - mover].note_reply_stones(stones);
        history.push_back({static_cast<int>(cells.size()), mover, used_before});
    }
    std::cout << "\n" << render(state) << "\n" << format_status(state) << "\n"
              << format_result(state) << "\n";
    return state.compare_final();
}

}  // namespace catchup
