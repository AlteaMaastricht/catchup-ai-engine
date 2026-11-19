"""Static board tables, loaded from libcatchup so they match the C++ engine."""

from ._lib import CatchupBoardTables, lib

_tables = CatchupBoardTables()
lib.catchup_board_tables(_tables)

BOARD_SIDE = 5
BOARD_RADIUS = 4
GRID = 9
NUM_CELLS = 61

LETTERS = "ABCDEFGHI"
NEIGHBOUR_DELTAS = ((1, 0), (-1, 0), (0, 1), (0, -1), (1, 1), (-1, -1))

WHITE = 0
BLACK = 1
COLOUR_NAMES = ("White", "Black")

EMPTY = 0
WHITE_STONE = 1
BLACK_STONE = 2
STONE = (WHITE_STONE, BLACK_STONE)

CELL_NAMES = tuple(_tables.cell_names[i].value.decode("ascii") for i in range(NUM_CELLS))
CELL_X = tuple(_tables.cell_x[i] for i in range(NUM_CELLS))
CELL_Y = tuple(_tables.cell_y[i] for i in range(NUM_CELLS))
CELL_COORDS = tuple(zip(CELL_X, CELL_Y))
NAME_TO_INDEX = {name: i for i, name in enumerate(CELL_NAMES)}

NEIGHBOURS = tuple(
    tuple(_tables.neighbours[c][i] for i in range(_tables.neighbour_count[c]))
    for c in range(NUM_CELLS)
)
NEIGHBOUR_COUNT = tuple(_tables.neighbour_count[i] for i in range(NUM_CELLS))
NEIGHBOUR_MASK = tuple(_tables.neighbour_mask[i] for i in range(NUM_CELLS))
RING = tuple(_tables.ring[i] for i in range(NUM_CELLS))
CENTRALITY = tuple(_tables.centrality[i] for i in range(NUM_CELLS))
STATIC_ORDER = tuple(_tables.static_order[i] for i in range(NUM_CELLS))
STATIC_RANK = tuple(_tables.static_rank[i] for i in range(NUM_CELLS))
FULL_MASK = int(_tables.full_mask)
INDEX_MASK_FROM = tuple(_tables.index_mask_from[i] for i in range(NUM_CELLS + 1))

DISPLAY_ROWS = tuple(
    (
        _tables.display_number[r],
        _tables.display_indent[r],
        tuple(_tables.display_cells[r][i] for i in range(_tables.display_ncells[r])),
    )
    for r in range(GRID)
)

ZOBRIST_CELL = tuple(
    tuple(_tables.zobrist_cell[p][c] for c in range(NUM_CELLS)) for p in range(2)
)
ZOBRIST_SIDE = int(_tables.zobrist_side)
ZOBRIST_ALLOWS_THREE = int(_tables.zobrist_allows_three)
ZOBRIST_PLACED = tuple(_tables.zobrist_placed[i] for i in range(4))


def on_board(x, y):
    return 0 <= x < GRID and 0 <= y < GRID and abs(x - y) <= BOARD_RADIUS


def hex_distance(x0, y0, x1, y1):
    dx = x1 - x0
    dy = y1 - y0
    if dx * dy >= 0:
        return max(abs(dx), abs(dy))
    return abs(dx) + abs(dy)


def parse_cell(text):
    idx = lib.catchup_parse_cell(text.encode("ascii") if isinstance(text, str) else text)
    if idx < 0:
        raise ValueError("%r is not a cell on the board" % (text,))
    return idx
