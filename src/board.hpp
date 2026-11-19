#pragma once

// Static Catchup board tables. Geometry matches catchup/board.py.
// Occupancy/side/allows_three keys: Python random.Random(0x0AC7CB5E17C0FFEE).
// Score-start and min-index keys: splitmix64 from seed 0x51A27001C47C4B5E.

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>

namespace catchup {

inline constexpr int BOARD_SIDE = 5;
inline constexpr int BOARD_RADIUS = 4;
inline constexpr int GRID = 9;
inline constexpr int NUM_CELLS = 61;

inline constexpr int WHITE = 0;
inline constexpr int BLACK = 1;

inline constexpr const char* COLOUR_NAMES[2] = {"White", "Black"};
inline constexpr const char LETTERS[] = "ABCDEFGHI";

inline constexpr int EMPTY = 0;
inline constexpr int WHITE_STONE = 1;
inline constexpr int BLACK_STONE = 2;
inline constexpr int STONE[2] = {WHITE_STONE, BLACK_STONE};

inline constexpr int WIN_SCORE = 10000000;

inline constexpr const char* CELL_NAMES[NUM_CELLS] = {"A1", "B1", "C1", "D1", "E1", "A2", "B2", "C2", "D2", "E2", "F2", "A3", "B3", "C3", "D3", "E3", "F3", "G3", "A4", "B4", "C4", "D4", "E4", "F4", "G4", "H4", "A5", "B5", "C5", "D5", "E5", "F5", "G5", "H5", "I5", "B6", "C6", "D6", "E6", "F6", "G6", "H6", "I6", "C7", "D7", "E7", "F7", "G7", "H7", "I7", "D8", "E8", "F8", "G8", "H8", "I8", "E9", "F9", "G9", "H9", "I9"};

inline constexpr int CELL_X[NUM_CELLS] = {
    0, 1, 2, 3, 4, 0, 1, 2, 3, 4, 5, 0,
    1, 2, 3, 4, 5, 6, 0, 1, 2, 3, 4, 5,
    6, 7, 0, 1, 2, 3, 4, 5, 6, 7, 8, 1,
    2, 3, 4, 5, 6, 7, 8, 2, 3, 4, 5, 6,
    7, 8, 3, 4, 5, 6, 7, 8, 4, 5, 6, 7,
    8
};
inline constexpr int CELL_Y[NUM_CELLS] = {
    0, 0, 0, 0, 0, 1, 1, 1, 1, 1, 1, 2,
    2, 2, 2, 2, 2, 2, 3, 3, 3, 3, 3, 3,
    3, 3, 4, 4, 4, 4, 4, 4, 4, 4, 4, 5,
    5, 5, 5, 5, 5, 5, 5, 6, 6, 6, 6, 6,
    6, 6, 7, 7, 7, 7, 7, 7, 8, 8, 8, 8,
    8
};

inline constexpr int NEIGHBOURS[NUM_CELLS][6] = {
    {1, 5, 6, -1, -1, -1},
    {0, 2, 6, 7, -1, -1},
    {1, 3, 7, 8, -1, -1},
    {2, 4, 8, 9, -1, -1},
    {3, 9, 10, -1, -1, -1},
    {0, 6, 11, 12, -1, -1},
    {0, 1, 5, 7, 12, 13},
    {1, 2, 6, 8, 13, 14},
    {2, 3, 7, 9, 14, 15},
    {3, 4, 8, 10, 15, 16},
    {4, 9, 16, 17, -1, -1},
    {5, 12, 18, 19, -1, -1},
    {5, 6, 11, 13, 19, 20},
    {6, 7, 12, 14, 20, 21},
    {7, 8, 13, 15, 21, 22},
    {8, 9, 14, 16, 22, 23},
    {9, 10, 15, 17, 23, 24},
    {10, 16, 24, 25, -1, -1},
    {11, 19, 26, 27, -1, -1},
    {11, 12, 18, 20, 27, 28},
    {12, 13, 19, 21, 28, 29},
    {13, 14, 20, 22, 29, 30},
    {14, 15, 21, 23, 30, 31},
    {15, 16, 22, 24, 31, 32},
    {16, 17, 23, 25, 32, 33},
    {17, 24, 33, 34, -1, -1},
    {18, 27, 35, -1, -1, -1},
    {18, 19, 26, 28, 35, 36},
    {19, 20, 27, 29, 36, 37},
    {20, 21, 28, 30, 37, 38},
    {21, 22, 29, 31, 38, 39},
    {22, 23, 30, 32, 39, 40},
    {23, 24, 31, 33, 40, 41},
    {24, 25, 32, 34, 41, 42},
    {25, 33, 42, -1, -1, -1},
    {26, 27, 36, 43, -1, -1},
    {27, 28, 35, 37, 43, 44},
    {28, 29, 36, 38, 44, 45},
    {29, 30, 37, 39, 45, 46},
    {30, 31, 38, 40, 46, 47},
    {31, 32, 39, 41, 47, 48},
    {32, 33, 40, 42, 48, 49},
    {33, 34, 41, 49, -1, -1},
    {35, 36, 44, 50, -1, -1},
    {36, 37, 43, 45, 50, 51},
    {37, 38, 44, 46, 51, 52},
    {38, 39, 45, 47, 52, 53},
    {39, 40, 46, 48, 53, 54},
    {40, 41, 47, 49, 54, 55},
    {41, 42, 48, 55, -1, -1},
    {43, 44, 51, 56, -1, -1},
    {44, 45, 50, 52, 56, 57},
    {45, 46, 51, 53, 57, 58},
    {46, 47, 52, 54, 58, 59},
    {47, 48, 53, 55, 59, 60},
    {48, 49, 54, 60, -1, -1},
    {50, 51, 57, -1, -1, -1},
    {51, 52, 56, 58, -1, -1},
    {52, 53, 57, 59, -1, -1},
    {53, 54, 58, 60, -1, -1},
    {54, 55, 59, -1, -1, -1}
};

inline constexpr int NEIGHBOUR_COUNT[NUM_CELLS] = {
    3, 4, 4, 4, 3, 4, 6, 6, 6, 6, 4, 4,
    6, 6, 6, 6, 6, 4, 4, 6, 6, 6, 6, 6,
    6, 4, 3, 6, 6, 6, 6, 6, 6, 6, 3, 4,
    6, 6, 6, 6, 6, 6, 4, 4, 6, 6, 6, 6,
    6, 4, 4, 6, 6, 6, 6, 4, 3, 4, 4, 4,
    3
};
inline constexpr std::uint64_t NEIGHBOUR_MASK[NUM_CELLS] = {
    UINT64_C(0x0000000000000062), UINT64_C(0x00000000000000c5), UINT64_C(0x000000000000018a), UINT64_C(0x0000000000000314),
    UINT64_C(0x0000000000000608), UINT64_C(0x0000000000001841), UINT64_C(0x00000000000030a3), UINT64_C(0x0000000000006146),
    UINT64_C(0x000000000000c28c), UINT64_C(0x0000000000018518), UINT64_C(0x0000000000030210), UINT64_C(0x00000000000c1020),
    UINT64_C(0x0000000000182860), UINT64_C(0x00000000003050c0), UINT64_C(0x000000000060a180), UINT64_C(0x0000000000c14300),
    UINT64_C(0x0000000001828600), UINT64_C(0x0000000003010400), UINT64_C(0x000000000c080800), UINT64_C(0x0000000018141800),
    UINT64_C(0x0000000030283000), UINT64_C(0x0000000060506000), UINT64_C(0x00000000c0a0c000), UINT64_C(0x0000000181418000),
    UINT64_C(0x0000000302830000), UINT64_C(0x0000000601020000), UINT64_C(0x0000000808040000), UINT64_C(0x00000018140c0000),
    UINT64_C(0x0000003028180000), UINT64_C(0x0000006050300000), UINT64_C(0x000000c0a0600000), UINT64_C(0x0000018140c00000),
    UINT64_C(0x0000030281800000), UINT64_C(0x0000060503000000), UINT64_C(0x0000040202000000), UINT64_C(0x000008100c000000),
    UINT64_C(0x0000182818000000), UINT64_C(0x0000305030000000), UINT64_C(0x000060a060000000), UINT64_C(0x0000c140c0000000),
    UINT64_C(0x0001828180000000), UINT64_C(0x0003050300000000), UINT64_C(0x0002020600000000), UINT64_C(0x0004101800000000),
    UINT64_C(0x000c283000000000), UINT64_C(0x0018506000000000), UINT64_C(0x0030a0c000000000), UINT64_C(0x0061418000000000),
    UINT64_C(0x00c2830000000000), UINT64_C(0x0081060000000000), UINT64_C(0x0108180000000000), UINT64_C(0x0314300000000000),
    UINT64_C(0x0628600000000000), UINT64_C(0x0c50c00000000000), UINT64_C(0x18a1800000000000), UINT64_C(0x1043000000000000),
    UINT64_C(0x020c000000000000), UINT64_C(0x0518000000000000), UINT64_C(0x0a30000000000000), UINT64_C(0x1460000000000000),
    UINT64_C(0x08c0000000000000)
};
inline constexpr int RING[NUM_CELLS] = {
    4, 4, 4, 4, 4, 4, 3, 3, 3, 3, 4, 4,
    3, 2, 2, 2, 3, 4, 4, 3, 2, 1, 1, 2,
    3, 4, 4, 3, 2, 1, 0, 1, 2, 3, 4, 4,
    3, 2, 1, 1, 2, 3, 4, 4, 3, 2, 2, 2,
    3, 4, 4, 3, 3, 3, 3, 4, 4, 4, 4, 4,
    4
};
inline constexpr int CENTRALITY[NUM_CELLS] = {
    0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 0, 0,
    1, 2, 2, 2, 1, 0, 0, 1, 2, 3, 3, 2,
    1, 0, 0, 1, 2, 3, 4, 3, 2, 1, 0, 0,
    1, 2, 3, 3, 2, 1, 0, 0, 1, 2, 2, 2,
    1, 0, 0, 1, 1, 1, 1, 0, 0, 0, 0, 0,
    0
};
inline constexpr int STATIC_ORDER[NUM_CELLS] = {
    30, 21, 22, 29, 31, 38, 39, 13, 14, 15, 20, 23,
    28, 32, 37, 40, 45, 46, 47, 6, 7, 8, 9, 12,
    16, 19, 24, 27, 33, 36, 41, 44, 48, 51, 52, 53,
    54, 0, 1, 2, 3, 4, 5, 10, 11, 17, 18, 25,
    26, 34, 35, 42, 43, 49, 50, 55, 56, 57, 58, 59,
    60
};
inline constexpr int STATIC_RANK[NUM_CELLS] = {
    37, 38, 39, 40, 41, 42, 19, 20, 21, 22, 43, 44,
    23, 7, 8, 9, 24, 45, 46, 25, 10, 1, 2, 11,
    26, 47, 48, 27, 12, 3, 0, 4, 13, 28, 49, 50,
    29, 14, 5, 6, 15, 30, 51, 52, 31, 16, 17, 18,
    32, 53, 54, 33, 34, 35, 36, 55, 56, 57, 58, 59,
    60
};

inline constexpr std::uint64_t FULL_MASK = UINT64_C(0x1fffffffffffffff);
inline constexpr std::uint64_t INDEX_MASK_FROM[NUM_CELLS + 1] = {
    UINT64_C(0x1fffffffffffffff),
    UINT64_C(0x1ffffffffffffffe),
    UINT64_C(0x1ffffffffffffffc),
    UINT64_C(0x1ffffffffffffff8),
    UINT64_C(0x1ffffffffffffff0),
    UINT64_C(0x1fffffffffffffe0),
    UINT64_C(0x1fffffffffffffc0),
    UINT64_C(0x1fffffffffffff80),
    UINT64_C(0x1fffffffffffff00),
    UINT64_C(0x1ffffffffffffe00),
    UINT64_C(0x1ffffffffffffc00),
    UINT64_C(0x1ffffffffffff800),
    UINT64_C(0x1ffffffffffff000),
    UINT64_C(0x1fffffffffffe000),
    UINT64_C(0x1fffffffffffc000),
    UINT64_C(0x1fffffffffff8000),
    UINT64_C(0x1fffffffffff0000),
    UINT64_C(0x1ffffffffffe0000),
    UINT64_C(0x1ffffffffffc0000),
    UINT64_C(0x1ffffffffff80000),
    UINT64_C(0x1ffffffffff00000),
    UINT64_C(0x1fffffffffe00000),
    UINT64_C(0x1fffffffffc00000),
    UINT64_C(0x1fffffffff800000),
    UINT64_C(0x1fffffffff000000),
    UINT64_C(0x1ffffffffe000000),
    UINT64_C(0x1ffffffffc000000),
    UINT64_C(0x1ffffffff8000000),
    UINT64_C(0x1ffffffff0000000),
    UINT64_C(0x1fffffffe0000000),
    UINT64_C(0x1fffffffc0000000),
    UINT64_C(0x1fffffff80000000),
    UINT64_C(0x1fffffff00000000),
    UINT64_C(0x1ffffffe00000000),
    UINT64_C(0x1ffffffc00000000),
    UINT64_C(0x1ffffff800000000),
    UINT64_C(0x1ffffff000000000),
    UINT64_C(0x1fffffe000000000),
    UINT64_C(0x1fffffc000000000),
    UINT64_C(0x1fffff8000000000),
    UINT64_C(0x1fffff0000000000),
    UINT64_C(0x1ffffe0000000000),
    UINT64_C(0x1ffffc0000000000),
    UINT64_C(0x1ffff80000000000),
    UINT64_C(0x1ffff00000000000),
    UINT64_C(0x1fffe00000000000),
    UINT64_C(0x1fffc00000000000),
    UINT64_C(0x1fff800000000000),
    UINT64_C(0x1fff000000000000),
    UINT64_C(0x1ffe000000000000),
    UINT64_C(0x1ffc000000000000),
    UINT64_C(0x1ff8000000000000),
    UINT64_C(0x1ff0000000000000),
    UINT64_C(0x1fe0000000000000),
    UINT64_C(0x1fc0000000000000),
    UINT64_C(0x1f80000000000000),
    UINT64_C(0x1f00000000000000),
    UINT64_C(0x1e00000000000000),
    UINT64_C(0x1c00000000000000),
    UINT64_C(0x1800000000000000),
    UINT64_C(0x1000000000000000),
    UINT64_C(0x0000000000000000)
};

inline constexpr std::uint64_t CELL_BIT[NUM_CELLS] = {
    UINT64_C(0x0000000000000001), UINT64_C(0x0000000000000002), UINT64_C(0x0000000000000004), UINT64_C(0x0000000000000008), UINT64_C(0x0000000000000010), UINT64_C(0x0000000000000020), UINT64_C(0x0000000000000040), UINT64_C(0x0000000000000080), UINT64_C(0x0000000000000100), UINT64_C(0x0000000000000200), UINT64_C(0x0000000000000400), UINT64_C(0x0000000000000800), UINT64_C(0x0000000000001000), UINT64_C(0x0000000000002000), UINT64_C(0x0000000000004000), UINT64_C(0x0000000000008000), UINT64_C(0x0000000000010000), UINT64_C(0x0000000000020000), UINT64_C(0x0000000000040000), UINT64_C(0x0000000000080000), UINT64_C(0x0000000000100000), UINT64_C(0x0000000000200000), UINT64_C(0x0000000000400000), UINT64_C(0x0000000000800000), UINT64_C(0x0000000001000000), UINT64_C(0x0000000002000000), UINT64_C(0x0000000004000000), UINT64_C(0x0000000008000000), UINT64_C(0x0000000010000000), UINT64_C(0x0000000020000000), UINT64_C(0x0000000040000000), UINT64_C(0x0000000080000000), UINT64_C(0x0000000100000000), UINT64_C(0x0000000200000000), UINT64_C(0x0000000400000000), UINT64_C(0x0000000800000000), UINT64_C(0x0000001000000000), UINT64_C(0x0000002000000000), UINT64_C(0x0000004000000000), UINT64_C(0x0000008000000000), UINT64_C(0x0000010000000000), UINT64_C(0x0000020000000000), UINT64_C(0x0000040000000000), UINT64_C(0x0000080000000000), UINT64_C(0x0000100000000000), UINT64_C(0x0000200000000000), UINT64_C(0x0000400000000000), UINT64_C(0x0000800000000000), UINT64_C(0x0001000000000000), UINT64_C(0x0002000000000000), UINT64_C(0x0004000000000000), UINT64_C(0x0008000000000000), UINT64_C(0x0010000000000000), UINT64_C(0x0020000000000000), UINT64_C(0x0040000000000000), UINT64_C(0x0080000000000000), UINT64_C(0x0100000000000000), UINT64_C(0x0200000000000000), UINT64_C(0x0400000000000000), UINT64_C(0x0800000000000000), UINT64_C(0x1000000000000000)
};

struct DisplayRow { int number; int indent; int ncells; int cells[9]; };
inline constexpr DisplayRow DISPLAY_ROWS[GRID] = {
    {9, 4, 5, {56, 57, 58, 59, 60, -1, -1, -1, -1}},
    {8, 3, 6, {50, 51, 52, 53, 54, 55, -1, -1, -1}},
    {7, 2, 7, {43, 44, 45, 46, 47, 48, 49, -1, -1}},
    {6, 1, 8, {35, 36, 37, 38, 39, 40, 41, 42, -1}},
    {5, 0, 9, {26, 27, 28, 29, 30, 31, 32, 33, 34}},
    {4, 1, 8, {18, 19, 20, 21, 22, 23, 24, 25, -1}},
    {3, 2, 7, {11, 12, 13, 14, 15, 16, 17, -1, -1}},
    {2, 3, 6, {5, 6, 7, 8, 9, 10, -1, -1, -1}},
    {1, 4, 5, {0, 1, 2, 3, 4, -1, -1, -1, -1}}
};

inline constexpr std::uint64_t ZOBRIST_CELL[2][NUM_CELLS] = {
    {
    UINT64_C(0xfd9f6d4cda0f359e),
    UINT64_C(0x222afd64b5a3dff3),
    UINT64_C(0xfdcea207e4051590),
    UINT64_C(0x108ba04e6344ea8c),
    UINT64_C(0xba2f6494ba998fbb),
    UINT64_C(0xb09fc3cedcc7e6e0),
    UINT64_C(0xa6b868abdf767bdd),
    UINT64_C(0x4a5e96bd2b1c9ff2),
    UINT64_C(0x6bcf69fbd50b0a8b),
    UINT64_C(0xabcb25d1b3921d9b),
    UINT64_C(0x04cd83f66f4cb932),
    UINT64_C(0xcd36de5e83f7e031),
    UINT64_C(0xc80cfc4e9485f52d),
    UINT64_C(0x193cac78bb56ee29),
    UINT64_C(0xb3023406a9fb566e),
    UINT64_C(0x7f439e84534ac242),
    UINT64_C(0xc25835a63cb522e6),
    UINT64_C(0xfa35b61a24a02ece),
    UINT64_C(0xa42cd83b7743c3d2),
    UINT64_C(0xe297ce5c20f461ca),
    UINT64_C(0x51b66a7772ebee49),
    UINT64_C(0xb0d0839a916c7a20),
    UINT64_C(0x964cad7e0068a2f1),
    UINT64_C(0x6f846de462f95530),
    UINT64_C(0x0c846af48207676d),
    UINT64_C(0x5ee5d2113feb104c),
    UINT64_C(0x946b875989efd8d8),
    UINT64_C(0x73f1c7a63594ecec),
    UINT64_C(0x0fc8754fd87316d9),
    UINT64_C(0x8dbbb743c3455eaa),
    UINT64_C(0xb260f7abeac3391e),
    UINT64_C(0x9f81e739e6395cc2),
    UINT64_C(0xd694a6fb901757e2),
    UINT64_C(0x5094ce490e2d4fc1),
    UINT64_C(0x77881753b7e2deb6),
    UINT64_C(0x0a879118fb94e914),
    UINT64_C(0x359f81518ffb7012),
    UINT64_C(0x3d403248a68e082e),
    UINT64_C(0x6758d7cf7f763f38),
    UINT64_C(0x355fc825a10545ed),
    UINT64_C(0xc0a240b30379ab25),
    UINT64_C(0x6e9363790faf4548),
    UINT64_C(0x7be68589bbb136a5),
    UINT64_C(0xa122e25a84ace3e7),
    UINT64_C(0xd6f8da9897eb071e),
    UINT64_C(0xa3d0b08e7a4165d4),
    UINT64_C(0x43e7beb4a2f0d29f),
    UINT64_C(0x7ebb5bff2a757338),
    UINT64_C(0x1365808ad1022ae6),
    UINT64_C(0x094cdc61aa587064),
    UINT64_C(0x5f14e4a7eac7fe83),
    UINT64_C(0x46c2da6bee344269),
    UINT64_C(0x5d739352f89eab93),
    UINT64_C(0x4456deb9a71296de),
    UINT64_C(0x1f288f6192039b50),
    UINT64_C(0x7b630052b2d895ff),
    UINT64_C(0x12f2b3430a7ba18e),
    UINT64_C(0x0947f0b65fc1d489),
    UINT64_C(0x81b8037adc02c5e1),
    UINT64_C(0x640b0a5d100ad7d3),
    UINT64_C(0xc16df3134fa4cf23)
    },
    {
    UINT64_C(0xe5266f2c7dce3b5a),
    UINT64_C(0x873d61b6309fe0e7),
    UINT64_C(0x1c778fcad6adb1cb),
    UINT64_C(0xf525b6b587587f96),
    UINT64_C(0x43a1c03b04d4eaf7),
    UINT64_C(0x9887f094fa91b749),
    UINT64_C(0xcd69240336038966),
    UINT64_C(0x78ee0e82ca8f4575),
    UINT64_C(0xdac986201821def8),
    UINT64_C(0x0200db15db6f8c31),
    UINT64_C(0xd5cfe286363b1cbd),
    UINT64_C(0x9a26044797942a73),
    UINT64_C(0xc494817ad8ac9057),
    UINT64_C(0x9748d5c59f70f07e),
    UINT64_C(0x2b0b54dceb5cce27),
    UINT64_C(0x14e09c48b609abf8),
    UINT64_C(0x02dbcd35c5f30f31),
    UINT64_C(0x10b38462e207f46e),
    UINT64_C(0x3e5cffc74535ed26),
    UINT64_C(0xa62c7180a54679ee),
    UINT64_C(0xca217912be69dd5f),
    UINT64_C(0x4cd6e96a21d0c0d6),
    UINT64_C(0xb12e9b5c92a08a09),
    UINT64_C(0x31b2a15145efa9ff),
    UINT64_C(0xc02110515a1e6c6f),
    UINT64_C(0x5610c42bb9d4be7a),
    UINT64_C(0x99e49160ace973c3),
    UINT64_C(0x571a5ab703e3ccb7),
    UINT64_C(0x1f92134ab44b8010),
    UINT64_C(0xfa30b2d0e0832a20),
    UINT64_C(0x9ee82c3d5100c00b),
    UINT64_C(0x7348f870fdb0e0a7),
    UINT64_C(0x882623760289226c),
    UINT64_C(0x8145e3c8bf2b94ce),
    UINT64_C(0x0a98cede3add77b6),
    UINT64_C(0xd3de4f1460329dcf),
    UINT64_C(0x1d11666b8b1e240f),
    UINT64_C(0x7b275935fa8963e2),
    UINT64_C(0x2af4c635c033a79a),
    UINT64_C(0x8635c028faaff6ca),
    UINT64_C(0xc2ae9ccd01a91a6d),
    UINT64_C(0xb4e8bad23baa423c),
    UINT64_C(0xd8a03509d715317d),
    UINT64_C(0x17c01e8008aebaae),
    UINT64_C(0x12b1da9d7e26f9e5),
    UINT64_C(0xa0738e74df64132c),
    UINT64_C(0xb08d81c730e6718e),
    UINT64_C(0x565ea9153d8bcd79),
    UINT64_C(0xb53a4119f2740583),
    UINT64_C(0x2f253976ecd24064),
    UINT64_C(0x227054af4946ea8f),
    UINT64_C(0x9c409e6da50154ba),
    UINT64_C(0x3fad4dd92b1dfc1a),
    UINT64_C(0x84f2967541083c15),
    UINT64_C(0x493270273b002da6),
    UINT64_C(0x10a108fff9959889),
    UINT64_C(0x29b67855fe79b627),
    UINT64_C(0x30ea317b8f7c01ce),
    UINT64_C(0xd54eeea3d9dd9f7d),
    UINT64_C(0x2fdd628826b2d586),
    UINT64_C(0x52e49ba58dc8e0b7)
    }
};

inline constexpr std::uint64_t ZOBRIST_SIDE = UINT64_C(0x0e07a37d6480a1f5);
inline constexpr std::uint64_t ZOBRIST_ALLOWS_THREE = UINT64_C(0xb009496c15b3cd99);
inline constexpr std::uint64_t ZOBRIST_PLACED[4] = {UINT64_C(0x9ebd3d8b957b3c98), UINT64_C(0xf4436d0ee4a435eb), UINT64_C(0x52c13eee96273ad8), UINT64_C(0xacd50fcdb4c287d4)};
inline constexpr std::uint64_t ZOBRIST_SCORE_START[62] = {
    UINT64_C(0xc37338ff387c50d3),
    UINT64_C(0x440d4a7105efbfef),
    UINT64_C(0xb39e4984d53415de),
    UINT64_C(0x852837a9f2328b23),
    UINT64_C(0x6729cf3263725483),
    UINT64_C(0x7a37a7a3b835e760),
    UINT64_C(0x65d1b27154b449de),
    UINT64_C(0xc209006f7ea62d63),
    UINT64_C(0x8837f3460bb01825),
    UINT64_C(0x8b48eb86efbe2e83),
    UINT64_C(0xb2eb2f4ca54eaeb9),
    UINT64_C(0x7d77728143d1dcb9),
    UINT64_C(0x887d86ea60490202),
    UINT64_C(0x70f07c5d972a67d5),
    UINT64_C(0xcb511b57572d4c13),
    UINT64_C(0x90f78b71c9e75cf3),
    UINT64_C(0x9ab52e128b1c0da4),
    UINT64_C(0xa9f2c7407463c35c),
    UINT64_C(0x5befb8c7e6c0ac90),
    UINT64_C(0xcd7374ff93673a3f),
    UINT64_C(0xa211c14f10370bab),
    UINT64_C(0x4de75d33d49c8ae1),
    UINT64_C(0x6db2a24924295734),
    UINT64_C(0xe116b05a0b8c7a63),
    UINT64_C(0x8420ac0c5f6a4d78),
    UINT64_C(0x4f76cd80c49d179c),
    UINT64_C(0xc14b04f4adb7cac3),
    UINT64_C(0x6d07b75ebc1b9077),
    UINT64_C(0xf50d185e1a8ff104),
    UINT64_C(0x8402358f1d7bf114),
    UINT64_C(0xbdf273c79759f9be),
    UINT64_C(0xbd96ecf5908bbb48),
    UINT64_C(0x4361bf7e91fe5342),
    UINT64_C(0x364525dca019f0c5),
    UINT64_C(0x9f0773f16b8183bf),
    UINT64_C(0x350c7fff05d95d3d),
    UINT64_C(0x4547db3c6d1cf074),
    UINT64_C(0x62f08b3b04ff5bb0),
    UINT64_C(0x10f5f2d2044bed19),
    UINT64_C(0x57097254cfafc48a),
    UINT64_C(0x15bca84a3746515b),
    UINT64_C(0x1f84f2510c563042),
    UINT64_C(0x1f257c51ab33ec67),
    UINT64_C(0x3195986b7b05ade9),
    UINT64_C(0x7c61f6a3fd31f9b3),
    UINT64_C(0x0387655f89a51be1),
    UINT64_C(0xf45d5b48d50b7340),
    UINT64_C(0x72e91849b02e8ee8),
    UINT64_C(0x8ff6ca9c8ac1a262),
    UINT64_C(0x9b98fc94d92d2827),
    UINT64_C(0x2e39663da168a284),
    UINT64_C(0x37e292bf52e9c60a),
    UINT64_C(0x7963fadd335a82ed),
    UINT64_C(0x2f1ca3742c01f4e0),
    UINT64_C(0x03e438a382ef818c),
    UINT64_C(0x2b5dad58a21e6bc3),
    UINT64_C(0x4ec858e107f91602),
    UINT64_C(0x4ccbaa6493f3ccce),
    UINT64_C(0x147e8c4ef49eb64e),
    UINT64_C(0x8b54ac1cc4a5863b),
    UINT64_C(0xafac4ed75f7f0343),
    UINT64_C(0xb88a54b9817ba438)
};
inline constexpr std::uint64_t ZOBRIST_MIN_INDEX[62] = {
    UINT64_C(0x846f8401ffc5ada0),
    UINT64_C(0x207ca016ca6a5a76),
    UINT64_C(0x5a96829f88922604),
    UINT64_C(0x9d12040d614489c5),
    UINT64_C(0xcdcd94d46fb5b691),
    UINT64_C(0xdea39871d597fca2),
    UINT64_C(0x2ae9abcecfe58f08),
    UINT64_C(0x88ba7b3489dbe2bf),
    UINT64_C(0x557dc5fa3cee01de),
    UINT64_C(0x9f4c7c44d9179836),
    UINT64_C(0x3b52b9801d9de96a),
    UINT64_C(0xdbb911901c051e78),
    UINT64_C(0x8ffbddefc43bf7d6),
    UINT64_C(0x52f5d14895a4ead8),
    UINT64_C(0x262da34204bb3717),
    UINT64_C(0x9cda9b2c293e24df),
    UINT64_C(0xdd9c5fb34c51ffbf),
    UINT64_C(0xba781b3063f42473),
    UINT64_C(0xa0bc1da3bf62edb3),
    UINT64_C(0x6b1d17557162e584),
    UINT64_C(0x22b827ffcaef03a7),
    UINT64_C(0x94993289e3410e5b),
    UINT64_C(0x37589805bde9d1c2),
    UINT64_C(0x03bdab0960d7a2f2),
    UINT64_C(0x33903cefe7862d78),
    UINT64_C(0x6ade20603746aa9d),
    UINT64_C(0xb719d36fff86d7be),
    UINT64_C(0x282d01afd96caf0b),
    UINT64_C(0xb321fce8aa2f1830),
    UINT64_C(0xc3dd38434d636dcf),
    UINT64_C(0xb2de15f6af75f88c),
    UINT64_C(0x6f1bb813c554460b),
    UINT64_C(0xb7a2d557b2f8c5af),
    UINT64_C(0xce156fd1751362bf),
    UINT64_C(0xecb068e9bfee5156),
    UINT64_C(0x0cb94de61966a969),
    UINT64_C(0xa7aa53367dd71512),
    UINT64_C(0xd2609a2a36fe70ae),
    UINT64_C(0x18706f03d188a2dd),
    UINT64_C(0x0d55203cdb72a52c),
    UINT64_C(0x4a64ad213644fe96),
    UINT64_C(0x8aa876ad5540b1cb),
    UINT64_C(0x5d0e3965a60a093d),
    UINT64_C(0x8289990f683d091d),
    UINT64_C(0x0fb0ae65059e5d13),
    UINT64_C(0xe76f2dda2689ef36),
    UINT64_C(0x774a5e3d56ce1168),
    UINT64_C(0x0d36f0a44c2e5479),
    UINT64_C(0x14662ce3342ac98f),
    UINT64_C(0xf160fa4dad0df5b1),
    UINT64_C(0x8a53a4d24883f4c5),
    UINT64_C(0xaee405d912030647),
    UINT64_C(0x40cd4ddc79e7f6c0),
    UINT64_C(0x0ff8304c5f260b4e),
    UINT64_C(0x122649d4d7fb70d0),
    UINT64_C(0xb97b6a22a693ed64),
    UINT64_C(0xd31bc2d80e789490),
    UINT64_C(0x0299613ad2e2f56a),
    UINT64_C(0x98b65f84f62ae0aa),
    UINT64_C(0x3830c5f3d7db4c0f),
    UINT64_C(0x470248bddd9034af),
    UINT64_C(0xb0c6245aeb328afc)
};

inline constexpr int CENTRE_CELL = 30;

inline bool on_board(int x, int y) {
    return 0 <= x && x < GRID && 0 <= y && y < GRID && std::abs(x - y) <= BOARD_RADIUS;
}

inline int hex_distance(int x0, int y0, int x1, int y1) {
    int dx = x1 - x0;
    int dy = y1 - y0;
    if (dx * dy >= 0) return std::max(std::abs(dx), std::abs(dy));
    return std::abs(dx) + std::abs(dy);
}

inline int popcnt(std::uint64_t x) {
    return static_cast<int>(__builtin_popcountll(x));
}

inline int ctz(std::uint64_t x) {
    return static_cast<int>(__builtin_ctzll(x));
}

inline int parse_cell(const std::string& text) {
    std::string key;
    for (char c : text) {
        if (c == ' ' || c == '\t') continue;
        key.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(c))));
    }
    for (int i = 0; i < NUM_CELLS; ++i) {
        if (key == CELL_NAMES[i]) return i;
    }
    throw std::invalid_argument(text + " is not a cell on the board");
}

inline int name_to_index(const char* name) {
    for (int i = 0; i < NUM_CELLS; ++i) {
        if (std::strcmp(CELL_NAMES[i], name) == 0) return i;
    }
    return -1;
}

inline constexpr int N_SYMMETRIES = 12;

inline constexpr int cell_at_xy(int x, int y) {
    for (int i = 0; i < NUM_CELLS; ++i) {
        if (CELL_X[i] == x && CELL_Y[i] == y) return i;
    }
    return -1;
}

// 60° clockwise about E5 in the board's (x, y) axial frame:
// neighbours of centre cycle D4 → E4 → F5 → F6 → E6 → D5.
inline constexpr void hex_rot60(int& x, int& y) {
    int dx = x - BOARD_RADIUS;
    int dy = y - BOARD_RADIUS;
    x = dx - dy + BOARD_RADIUS;
    y = dx + BOARD_RADIUS;
}

// Reflection through the D4–E5–F6 diagonal (swap the two axes).
inline constexpr void hex_reflect(int& x, int& y) {
    int dx = x - BOARD_RADIUS;
    int dy = y - BOARD_RADIUS;
    x = dy + BOARD_RADIUS;
    y = dx + BOARD_RADIUS;
}

struct SymTable {
    int perm[N_SYMMETRIES][NUM_CELLS];
};

inline constexpr SymTable make_sym_table() {
    SymTable t{};
    for (int g = 0; g < N_SYMMETRIES; ++g) {
        for (int c = 0; c < NUM_CELLS; ++c) {
            int x = CELL_X[c];
            int y = CELL_Y[c];
            int rot = g;
            if (g >= 6) {
                hex_reflect(x, y);
                rot = g - 6;
            }
            for (int i = 0; i < rot; ++i) hex_rot60(x, y);
            t.perm[g][c] = cell_at_xy(x, y);
        }
    }
    return t;
}

inline constexpr SymTable SYM = make_sym_table();

}  // namespace catchup
