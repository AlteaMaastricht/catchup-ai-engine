"""C ABI guards: struct layout check and error codes instead of C++ exceptions.

These tests were added during repository preparation (see docs/PROVENANCE.md).
"""

import unittest
from ctypes import c_int, sizeof

from catchup._lib import (
    CatchupBoardTables, CatchupEngineConfig, CatchupSearchStats,
    CatchupStateData, lib,
)
from catchup.state import GameState, IllegalMove


class TestAbiLayout(unittest.TestCase):

    def test_ctypes_struct_sizes_match_the_c_header(self):
        native = (c_int * 4)()
        lib.catchup_abi_sizes(native)
        self.assertEqual(list(native), [
            sizeof(CatchupStateData), sizeof(CatchupBoardTables),
            sizeof(CatchupEngineConfig), sizeof(CatchupSearchStats),
        ])


class TestErrorCodes(unittest.TestCase):

    def test_place_rejects_off_board_and_occupied_cells(self):
        state = GameState()
        self.assertEqual(lib.catchup_state_place(state._ptr, -1), -1)
        self.assertEqual(lib.catchup_state_place(state._ptr, 61), -1)
        state.play_turn([30])
        self.assertEqual(lib.catchup_state_place(state._ptr, 30), -1)
        with self.assertRaises(IllegalMove):
            state.place(30)

    def test_undo_on_a_fresh_state_is_an_error_not_a_crash(self):
        state = GameState()
        self.assertEqual(lib.catchup_state_unplace(state._ptr), -1)
        self.assertEqual(lib.catchup_state_unend_turn(state._ptr), -1)
        self.assertEqual(lib.catchup_state_undo_turn(state._ptr, 1), -1)
        with self.assertRaises(RuntimeError):
            state.unplace()

    def test_valid_place_and_undo_still_work(self):
        state = GameState()
        state.place(30)
        self.assertEqual(state.colour[30], 1)
        state.unplace()
        self.assertEqual(state.colour[30], 0)
        state.play_turn([30])
        state.undo_turn(1)
        self.assertEqual(state.empty_count, 61)

    def test_null_pointers_are_rejected(self):
        self.assertEqual(lib.catchup_state_place(None, 0), -1)
        self.assertEqual(lib.catchup_engine_find_turn(None, None, 0.1, None), -1)
        self.assertEqual(lib.catchup_state_find(None, 0), -1)


if __name__ == "__main__":
    unittest.main()
