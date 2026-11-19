#include "check.hpp"
#include "board.hpp"

#include <algorithm>
#include <set>
#include <string>
#include <vector>

using namespace catchup;

void run_board_tests() {
    CHECK_EQ(NUM_CELLS, 61);
    std::set<std::string> names;
    for (int i = 0; i < NUM_CELLS; ++i) names.insert(CELL_NAMES[i]);
    CHECK_EQ(static_cast<int>(names.size()), 61);

    int lengths[GRID];
    for (int r = 0; r < GRID; ++r) lengths[r] = DISPLAY_ROWS[r].ncells;
    int expect_len[] = {5, 6, 7, 8, 9, 8, 7, 6, 5};
    for (int i = 0; i < GRID; ++i) CHECK_EQ(lengths[i], expect_len[i]);

    auto row_names = [](int number) {
        std::vector<std::string> out;
        for (int r = 0; r < GRID; ++r) {
            if (DISPLAY_ROWS[r].number == number) {
                for (int i = 0; i < DISPLAY_ROWS[r].ncells; ++i) {
                    out.emplace_back(CELL_NAMES[DISPLAY_ROWS[r].cells[i]]);
                }
            }
        }
        return out;
    };
    CHECK(row_names(1) == std::vector<std::string>({"A1", "B1", "C1", "D1", "E1"}));
    CHECK(row_names(9) == std::vector<std::string>({"E9", "F9", "G9", "H9", "I9"}));

    for (const char* n : {"A1", "E1", "A5", "E5", "I5", "E9", "I9"}) {
        CHECK(name_to_index(n) >= 0);
    }
    for (const char* n : {"A6", "A9", "I1", "I4", "D9", "F1"}) {
        CHECK_EQ(name_to_index(n), -1);
    }

    auto neighbours_of = [](const char* name) {
        int cell = name_to_index(name);
        std::vector<std::string> out;
        for (int i = 0; i < NEIGHBOUR_COUNT[cell]; ++i) {
            out.emplace_back(CELL_NAMES[NEIGHBOURS[cell][i]]);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    CHECK(neighbours_of("E5") ==
          std::vector<std::string>({"D4", "D5", "E4", "E6", "F5", "F6"}));
    CHECK(neighbours_of("C5") ==
          std::vector<std::string>({"B4", "B5", "C4", "C6", "D5", "D6"}));
    CHECK(neighbours_of("A1") == std::vector<std::string>({"A2", "B1", "B2"}));
    CHECK(neighbours_of("I9") == std::vector<std::string>({"H8", "H9", "I8"}));
    CHECK(neighbours_of("A5") == std::vector<std::string>({"A4", "B5", "B6"}));

    for (int cell = 0; cell < NUM_CELLS; ++cell) {
        for (int i = 0; i < NEIGHBOUR_COUNT[cell]; ++i) {
            int n = NEIGHBOURS[cell][i];
            bool found = false;
            for (int j = 0; j < NEIGHBOUR_COUNT[n]; ++j) {
                if (NEIGHBOURS[n][j] == cell) found = true;
            }
            CHECK(found);
        }
        std::uint64_t mask = 0;
        for (int i = 0; i < NEIGHBOUR_COUNT[cell]; ++i) {
            mask |= std::uint64_t{1} << NEIGHBOURS[cell][i];
        }
        CHECK_EQ(NEIGHBOUR_MASK[cell], mask);
        CHECK_EQ(popcnt(mask), NEIGHBOUR_COUNT[cell]);
    }

    int deg[7] = {};
    for (int c = 0; c < NUM_CELLS; ++c) deg[NEIGHBOUR_COUNT[c]] += 1;
    CHECK_EQ(deg[3], 6);
    CHECK_EQ(deg[4], 18);
    CHECK_EQ(deg[6], 37);

    int rings[5] = {};
    for (int c = 0; c < NUM_CELLS; ++c) rings[RING[c]] += 1;
    CHECK_EQ(rings[0], 1);
    CHECK_EQ(rings[1], 6);
    CHECK_EQ(rings[2], 12);
    CHECK_EQ(rings[3], 18);
    CHECK_EQ(rings[4], 24);

    int best = 0;
    for (int c = 1; c < NUM_CELLS; ++c) {
        if (CENTRALITY[c] > CENTRALITY[best]) best = c;
    }
    CHECK(std::string(CELL_NAMES[best]) == "E5");
    CHECK_EQ(CENTRE_CELL, best);

    std::vector<int> order(STATIC_ORDER, STATIC_ORDER + NUM_CELLS);
    std::vector<int> sorted = order;
    std::sort(sorted.begin(), sorted.end());
    for (int i = 0; i < NUM_CELLS; ++i) CHECK_EQ(sorted[i], i);
    CHECK(std::string(CELL_NAMES[STATIC_ORDER[0]]) == "E5");
    for (int i = 1; i < NUM_CELLS; ++i) {
        CHECK(CENTRALITY[STATIC_ORDER[i - 1]] >= CENTRALITY[STATIC_ORDER[i]]);
    }
    for (int i = 0; i < NUM_CELLS; ++i) {
        CHECK_EQ(STATIC_RANK[STATIC_ORDER[i]], i);
    }
    CHECK_EQ(popcnt(FULL_MASK), NUM_CELLS);
    CHECK_EQ(INDEX_MASK_FROM[0], FULL_MASK);
    CHECK_EQ(INDEX_MASK_FROM[NUM_CELLS], 0u);
    for (int lower : {0, 1, 17, 40, 60, 61}) {
        std::uint64_t mask = INDEX_MASK_FROM[lower];
        for (int cell = 0; cell < NUM_CELLS; ++cell) {
            int bit = static_cast<int>((mask >> cell) & 1);
            CHECK_EQ(bit, cell >= lower ? 1 : 0);
        }
    }

    std::set<std::uint64_t> keys;
    for (int p = 0; p < 2; ++p)
        for (int c = 0; c < NUM_CELLS; ++c) keys.insert(ZOBRIST_CELL[p][c]);
    keys.insert(ZOBRIST_SIDE);
    keys.insert(ZOBRIST_ALLOWS_THREE);
    for (int i = 0; i < 4; ++i) keys.insert(ZOBRIST_PLACED[i]);
    for (int i = 0; i < 62; ++i) keys.insert(ZOBRIST_SCORE_START[i]);
    for (int i = 0; i < 62; ++i) keys.insert(ZOBRIST_MIN_INDEX[i]);
    CHECK_EQ(static_cast<int>(keys.size()), 2 * NUM_CELLS + 2 + 4 + 62 + 62);

    for (int c = 0; c < NUM_CELLS; ++c) {
        CHECK_EQ(parse_cell(CELL_NAMES[c]), c);
    }
    CHECK_EQ(parse_cell(" e5 "), name_to_index("E5"));
    bool threw = false;
    try { parse_cell("A6"); } catch (const std::invalid_argument&) { threw = true; }
    CHECK(threw);

    CHECK_EQ(SYM.perm[0][CENTRE_CELL], CENTRE_CELL);
    for (int g = 0; g < N_SYMMETRIES; ++g) {
        bool seen[NUM_CELLS] = {};
        int mapped_neighbours = 0;
        for (int c = 0; c < NUM_CELLS; ++c) {
            int img = SYM.perm[g][c];
            CHECK_GE(img, 0);
            CHECK_LT(img, NUM_CELLS);
            CHECK(!seen[img]);
            seen[img] = true;
        }
        for (int i = 0; i < NEIGHBOUR_COUNT[CENTRE_CELL]; ++i) {
            int n = NEIGHBOURS[CENTRE_CELL][i];
            int img = SYM.perm[g][n];
            bool neighbour = false;
            for (int j = 0; j < NEIGHBOUR_COUNT[CENTRE_CELL]; ++j) {
                if (NEIGHBOURS[CENTRE_CELL][j] == img) neighbour = true;
            }
            CHECK(neighbour);
            mapped_neighbours += 1;
        }
        CHECK_EQ(mapped_neighbours, 6);
        CHECK_EQ(SYM.perm[g][CENTRE_CELL], CENTRE_CELL);
    }
    CHECK(std::string(CELL_NAMES[SYM.perm[1][name_to_index("D4")]]) == "E4");
}
