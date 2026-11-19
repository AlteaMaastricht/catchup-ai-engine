#include "check.hpp"
#include "ui.hpp"

#include <algorithm>
#include <sstream>
#include <string>
#include <vector>

using namespace catchup;

void run_ui_tests() {
    {
        std::string picture = render(GameState());
        std::vector<std::string> lines;
        std::istringstream iss(picture);
        std::string line;
        while (std::getline(iss, line)) lines.push_back(line);
        CHECK_EQ(static_cast<int>(lines.size()), 9);
        std::vector<int> labels;
        for (auto& l : lines) {
            std::istringstream ls(l);
            int n;
            ls >> n;
            labels.push_back(n);
        }
        CHECK(labels == std::vector<int>({9, 8, 7, 6, 5, 4, 3, 2, 1}));
        CHECK(lines[0].find("e f g h i") != std::string::npos);
        CHECK(lines[4].find("a") != std::string::npos);
        CHECK(lines.back().find("a b c d e") != std::string::npos);
    }

    {
        GameState state;
        state.play_turn({name_to_index("E5")});
        std::string picture = render(state);
        std::istringstream iss(picture);
        std::string line, equator;
        while (std::getline(iss, line)) {
            std::istringstream ls(line);
            int n;
            ls >> n;
            if (n == 5) equator = line;
        }
        CHECK(equator.find("O") != std::string::npos);
        CHECK_EQ(static_cast<int>(std::count(equator.begin(), equator.end(), 'O')), 1);
    }

    {
        GameState state;
        CHECK(parse_turn("e5", state) == std::vector<int>({name_to_index("E5")}));
        state.play_turn({name_to_index("E5")});
        CHECK(parse_turn("A1, b1", state) ==
              std::vector<int>({name_to_index("A1"), name_to_index("B1")}));
    }

    {
        GameState state;
        bool threw = false;
        try { parse_turn("E5 D4", state); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { parse_turn("Z0", state); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        state.play_turn({name_to_index("E5")});
        threw = false;
        try { parse_turn("E5", state); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { parse_turn("A1 A1", state); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
        threw = false;
        try { parse_turn("", state); } catch (const std::invalid_argument&) { threw = true; }
        CHECK(threw);
    }

    CHECK(format_turn({name_to_index("E5"), name_to_index("D4")}) == std::string("E5 D4"));

    {
        std::string text = format_result_from_sizes({6, 5, 5, 3}, {6, 5, 4, 3});
        CHECK(text.find("White wins") != std::string::npos);
        CHECK(text.find("third largest") != std::string::npos);
    }
}
