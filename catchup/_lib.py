"""Load libcatchup and declare the C ABI."""

import os
from ctypes import (
    POINTER, Structure, c_char, c_char_p, c_double, c_int, c_uint8, c_uint64,
    c_void_p, cdll, sizeof,
)
from pathlib import Path


class CatchupStateData(Structure):
    _fields_ = [
        ("colour", c_uint8 * 61),
        ("bb", c_uint64 * 2),
        ("adjacency", (c_uint8 * 61) * 2),
        ("touch", c_uint64 * 2),
        ("join", c_uint64 * 2),
        ("empty_count", c_int),
        ("parent", c_int * 61),
        ("size", c_int * 61),
        ("size_n", (c_int * 62) * 2),
        ("lib_root", c_uint64 * 61),
        ("largest", c_int * 2),
        ("second", c_int * 2),
        ("largest_root", c_int * 2),
        ("sumsq", c_int * 2),
        ("nstones", c_int * 2),
        ("centrality", c_int * 2),
        ("frontier", c_int * 2),
        ("to_move", c_int),
        ("turn_index", c_int),
        ("placed_this_turn", c_int),
        ("max_this_turn", c_int),
        ("score_at_turn_start", c_int),
        ("score_increased", c_int * 2),
        ("allows_three", c_int),
        ("zobrist", c_uint64),
        ("undo_n", c_int),
        ("turn_undo_n", c_int),
        ("lib_undo_n", c_int),
    ]


class CatchupBoardTables(Structure):
    _fields_ = [
        ("cell_names", (c_char * 4) * 61),
        ("cell_x", c_int * 61),
        ("cell_y", c_int * 61),
        ("neighbours", (c_int * 6) * 61),
        ("neighbour_count", c_int * 61),
        ("neighbour_mask", c_uint64 * 61),
        ("ring", c_int * 61),
        ("centrality", c_int * 61),
        ("static_order", c_int * 61),
        ("static_rank", c_int * 61),
        ("full_mask", c_uint64),
        ("index_mask_from", c_uint64 * 62),
        ("zobrist_cell", (c_uint64 * 61) * 2),
        ("zobrist_side", c_uint64),
        ("zobrist_allows_three", c_uint64),
        ("zobrist_placed", c_uint64 * 4),
        ("centre_cell", c_int),
        ("display_number", c_int * 9),
        ("display_indent", c_int * 9),
        ("display_ncells", c_int * 9),
        ("display_cells", (c_int * 9) * 9),
    ]


class CatchupEngineConfig(Structure):
    _fields_ = [
        ("use_tt", c_int), ("use_pvs", c_int), ("use_killers", c_int),
        ("use_history", c_int), ("use_aspiration", c_int), ("use_lmr", c_int),
        ("use_opening_book", c_int),
        ("max_depth", c_int),
        ("aspiration_delta", c_int),
        ("w_largest", c_int), ("w_sumsq", c_int), ("w_frontier", c_int),
        ("w_centre", c_int), ("w_tempo", c_int),
        ("w_pending", c_int), ("w_liberty", c_int), ("w_stones", c_int),
        ("w_second", c_int), ("w_join", c_int),
        ("w_crit", c_int), ("w_third", c_int), ("w_fourth", c_int),
        ("use_rfp", c_int), ("use_lmp", c_int), ("use_iid", c_int),
        ("w_gift", c_int),
        ("w_reachable", c_int),
        ("tt_bits", c_int),
        ("use_countermove", c_int),
        ("use_stable_stop", c_int),
    ]


class CatchupSearchStats(Structure):
    _fields_ = [
        ("nodes", c_int),
        ("depth", c_int),
        ("value", c_int),
        ("completed", c_int),
        ("elapsed", c_double),
        ("turn", c_int * 3),
        ("turn_n", c_int),
    ]


def _find_library():
    # Explicit override, e.g. for an out-of-tree build or CI:
    #   CATCHUP_LIB=/path/to/libcatchup.so python3 -m unittest ...
    override = os.environ.get("CATCHUP_LIB")
    if override:
        if not Path(override).is_file():
            raise OSError("CATCHUP_LIB points to a missing file: %s" % override)
        return override
    pkg = Path(__file__).resolve().parent
    names = ("libcatchup.dylib", "libcatchup.so", "catchup.dll", "libcatchup.dll")
    roots = [
        pkg,
        pkg.parent / "build",
        pkg.parent / "build" / "Release",
        pkg.parent / "build" / "Debug",
    ]
    for root in roots:
        for name in names:
            path = root / name
            if path.exists():
                return str(path)
    raise OSError(
        "libcatchup not found. Build it first:\n"
        "  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build"
    )


def _check_abi(lib):
    """Fail loudly if the hand-written ctypes structs drift from c_api.h."""
    lib.catchup_abi_sizes.argtypes = [POINTER(c_int)]
    lib.catchup_abi_sizes.restype = None
    native = (c_int * 4)()
    lib.catchup_abi_sizes(native)
    mirrored = (
        ("CatchupStateData", sizeof(CatchupStateData)),
        ("CatchupBoardTables", sizeof(CatchupBoardTables)),
        ("CatchupEngineConfig", sizeof(CatchupEngineConfig)),
        ("CatchupSearchStats", sizeof(CatchupSearchStats)),
    )
    for i, (name, size) in enumerate(mirrored):
        if native[i] != size:
            raise OSError("ABI mismatch for %s: C says %d bytes, ctypes says %d"
                          % (name, native[i], size))


def _bind():
    lib = cdll.LoadLibrary(_find_library())
    _check_abi(lib)
    lib.catchup_board_tables.argtypes = [POINTER(CatchupBoardTables)]
    lib.catchup_board_tables.restype = None
    lib.catchup_parse_cell.argtypes = [c_char_p]
    lib.catchup_parse_cell.restype = c_int

    lib.catchup_state_new.argtypes = []
    lib.catchup_state_new.restype = c_void_p
    lib.catchup_state_free.argtypes = [c_void_p]
    lib.catchup_state_free.restype = None
    lib.catchup_state_clone.argtypes = [c_void_p]
    lib.catchup_state_clone.restype = c_void_p
    lib.catchup_state_get.argtypes = [c_void_p, POINTER(CatchupStateData)]
    lib.catchup_state_get.restype = None
    lib.catchup_state_set.argtypes = [c_void_p, POINTER(CatchupStateData)]
    lib.catchup_state_set.restype = None
    lib.catchup_state_place.argtypes = [c_void_p, c_int]
    lib.catchup_state_place.restype = c_int
    lib.catchup_state_unplace.argtypes = [c_void_p]
    lib.catchup_state_unplace.restype = c_int
    lib.catchup_state_end_turn.argtypes = [c_void_p]
    lib.catchup_state_end_turn.restype = c_int
    lib.catchup_state_unend_turn.argtypes = [c_void_p]
    lib.catchup_state_unend_turn.restype = c_int
    lib.catchup_state_play_turn.argtypes = [c_void_p, POINTER(c_int), c_int, c_char_p, c_int]
    lib.catchup_state_play_turn.restype = c_int
    lib.catchup_state_undo_turn.argtypes = [c_void_p, c_int]
    lib.catchup_state_undo_turn.restype = c_int
    lib.catchup_state_find.argtypes = [c_void_p, c_int]
    lib.catchup_state_find.restype = c_int
    lib.catchup_state_largest_lib_mask.argtypes = [c_void_p, c_int]
    lib.catchup_state_largest_lib_mask.restype = c_uint64
    lib.catchup_state_largest_liberties.argtypes = [c_void_p, c_int]
    lib.catchup_state_largest_liberties.restype = c_int
    lib.catchup_state_max_reachable.argtypes = [c_void_p, c_int]
    lib.catchup_state_max_reachable.restype = c_int
    lib.catchup_state_largest_reachable.argtypes = [c_void_p, c_int]
    lib.catchup_state_largest_reachable.restype = c_int
    lib.catchup_state_score.argtypes = [c_void_p, c_int]
    lib.catchup_state_score.restype = c_int
    lib.catchup_state_group_sizes.argtypes = [c_void_p, c_int, POINTER(c_int), c_int]
    lib.catchup_state_group_sizes.restype = c_int
    lib.catchup_state_compare_final.argtypes = [c_void_p]
    lib.catchup_state_compare_final.restype = c_int
    lib.catchup_state_terminal_score.argtypes = [c_void_p, c_int]
    lib.catchup_state_terminal_score.restype = c_int
    lib.catchup_state_legal_turn_sizes.argtypes = [c_void_p, POINTER(c_int), c_int]
    lib.catchup_state_legal_turn_sizes.restype = c_int
    lib.catchup_state_is_over.argtypes = [c_void_p]
    lib.catchup_state_is_over.restype = c_int
    lib.catchup_state_placements_left.argtypes = [c_void_p]
    lib.catchup_state_placements_left.restype = c_int

    lib.catchup_clock_new.argtypes = [c_double, c_double]
    lib.catchup_clock_new.restype = c_void_p
    lib.catchup_clock_free.argtypes = [c_void_p]
    lib.catchup_clock_remaining.argtypes = [c_void_p]
    lib.catchup_clock_remaining.restype = c_double
    lib.catchup_clock_allocate.argtypes = [c_void_p, c_void_p]
    lib.catchup_clock_allocate.restype = c_double
    lib.catchup_clock_consume.argtypes = [c_void_p, c_double]
    lib.catchup_clock_note_own.argtypes = [c_void_p, c_int]
    lib.catchup_clock_note_reply.argtypes = [c_void_p, c_int]
    lib.catchup_clock_undo_note.argtypes = [c_void_p]
    lib.catchup_clock_get_used.argtypes = [c_void_p]
    lib.catchup_clock_get_used.restype = c_double
    lib.catchup_clock_set_used.argtypes = [c_void_p, c_double]
    lib.catchup_clock_get_total.argtypes = [c_void_p]
    lib.catchup_clock_get_total.restype = c_double
    lib.catchup_clock_get_reserve.argtypes = [c_void_p]
    lib.catchup_clock_get_reserve.restype = c_double

    lib.catchup_engine_new.argtypes = [POINTER(CatchupEngineConfig)]
    lib.catchup_engine_new.restype = c_void_p
    lib.catchup_engine_free.argtypes = [c_void_p]
    lib.catchup_engine_new_game.argtypes = [c_void_p]
    lib.catchup_engine_set_deadline.argtypes = [c_void_p, c_double]
    lib.catchup_engine_get_deadline.argtypes = [c_void_p]
    lib.catchup_engine_get_deadline.restype = c_double
    lib.catchup_engine_find_turn.argtypes = [
        c_void_p, c_void_p, c_double, POINTER(CatchupSearchStats)]
    lib.catchup_engine_find_turn.restype = c_int
    lib.catchup_engine_set_eval_weights.argtypes = [c_void_p, POINTER(c_int)]
    lib.catchup_engine_set_eval_weights.restype = None
    lib.catchup_engine_get_eval_weights.argtypes = [c_void_p, POINTER(c_int)]
    lib.catchup_engine_get_eval_weights.restype = None
    lib.catchup_engine_evaluate.argtypes = [c_void_p, c_void_p, c_int]
    lib.catchup_engine_evaluate.restype = c_int
    return lib


lib = _bind()
