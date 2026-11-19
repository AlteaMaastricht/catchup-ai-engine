"""Geometry of the visual hex board, clock formatting, and the sanity checker."""

import unittest

from catchup import board as B
from catchup.gui import (
    HEX_SIZE,
    cell_at,
    cell_center,
    cell_offset,
    format_clock,
    format_game_caption,
    hex_vertices,
    parse_time_limit,
    rewind_clock_history,
    sanity_check,
)
from catchup.search import Clock
from catchup.state import GameState
from catchup.board import BLACK, WHITE


class TestHexGeometry(unittest.TestCase):

    def test_every_cell_has_a_distinct_centre(self):
        origin = (0.0, 0.0)
        centres = [cell_center(cell, origin) for cell in range(B.NUM_CELLS)]
        self.assertEqual(len(set(centres)), B.NUM_CELLS)

    def test_c5_neighbours_are_the_six_nearest_centres(self):
        origin = (0.0, 0.0)
        c5 = B.parse_cell("C5")
        cx, cy = cell_center(c5, origin)
        distances = []
        for cell in range(B.NUM_CELLS):
            if cell == c5:
                continue
            px, py = cell_center(cell, origin)
            d2 = (px - cx) ** 2 + (py - cy) ** 2
            distances.append((d2, cell))
        distances.sort()
        nearest = set(cell for _, cell in distances[:6])
        self.assertEqual(nearest, set(B.NEIGHBOURS[c5]))

    def test_neighbour_centres_are_one_hex_apart(self):
        origin = (0.0, 0.0)
        c5 = B.parse_cell("C5")
        cx, cy = cell_center(c5, origin)
        for neighbour in B.NEIGHBOURS[c5]:
            px, py = cell_center(neighbour, origin)
            dist = ((px - cx) ** 2 + (py - cy) ** 2) ** 0.5
            self.assertAlmostEqual(dist, HEX_SIZE, places=6)

    def test_cell_at_round_trips_every_centre(self):
        origin = (200.0, 180.0)
        for cell in range(B.NUM_CELLS):
            px, py = cell_center(cell, origin)
            self.assertEqual(cell_at(px, py, origin), cell)

    def test_cell_at_misses_far_away(self):
        origin = (0.0, 0.0)
        self.assertIsNone(cell_at(10_000.0, 10_000.0, origin))

    def test_higher_row_numbers_sit_higher_on_the_canvas(self):
        # Canvas y grows downward, so E9 must have a smaller y than E1.
        origin = (0.0, 0.0)
        e9 = cell_center(B.parse_cell("E9"), origin)
        e1 = cell_center(B.parse_cell("E1"), origin)
        self.assertLess(e9[1], e1[1])

    def test_hex_vertices_are_six_points(self):
        pts = hex_vertices(0.0, 0.0)
        self.assertEqual(len(pts), 12)
        # Pointy-top: one vertex is directly above the centre.
        tops = [(pts[i], pts[i + 1]) for i in range(0, 12, 2)]
        self.assertTrue(any(abs(x) < 1e-9 and y < 0 for x, y in tops))

    def test_offset_matches_centre_without_origin(self):
        for cell in (0, 30, 60):
            self.assertEqual(cell_offset(cell), cell_center(cell, (0.0, 0.0)))


class TestFormatClock(unittest.TestCase):

    def test_ten_minutes(self):
        self.assertEqual(format_clock(600.0), "10:00.0")

    def test_minutes_and_tenths(self):
        self.assertEqual(format_clock(65.4), "1:05.4")

    def test_negative_clamps_to_zero(self):
        self.assertEqual(format_clock(-3.0), "0:00.0")


class TestTimeLimitField(unittest.TestCase):

    def test_parses_and_clamps(self):
        self.assertEqual(parse_time_limit("30"), 30.0)
        self.assertEqual(parse_time_limit("5"), 10.0)
        self.assertEqual(parse_time_limit("99999"), 3600.0)
        self.assertEqual(parse_time_limit("nope"), 600.0)
        self.assertEqual(parse_time_limit(""), 600.0)

    def test_caption_follows_the_budget(self):
        self.assertEqual(format_game_caption(600.0), "hexhex-5  ·  10-minute game")
        self.assertEqual(format_game_caption(30.0), "hexhex-5  ·  30s / side")
        self.assertEqual(format_game_caption(90.0), "hexhex-5  ·  90s / side")

    def test_shorter_clock_allocates_less_think_time(self):
        state = GameState()
        short = Clock(30.0).allocate(state)
        long = Clock(600.0).allocate(state)
        self.assertLess(short, long)
        self.assertGreater(short, 0.0)


class TestRewindClockHistory(unittest.TestCase):

    def test_undo_pair_restores_both_clocks(self):
        clocks = {WHITE: Clock(600.0), BLACK: Clock(600.0)}
        history = []
        history.append((1, WHITE, 0.0))
        clocks[WHITE].used = 12.0
        history.append((2, BLACK, 0.0))
        clocks[BLACK].used = 45.5
        counts = rewind_clock_history(clocks, history, 2)
        self.assertEqual(counts, [2, 1])
        self.assertEqual(clocks[BLACK].used, 0.0)
        self.assertEqual(clocks[WHITE].used, 0.0)
        self.assertEqual(history, [])

    def test_single_undo_only_rewinds_the_last_mover(self):
        clocks = {WHITE: Clock(600.0), BLACK: Clock(600.0)}
        clocks[WHITE].used = 10.0
        clocks[BLACK].used = 30.0
        history = [(1, WHITE, 0.0), (2, BLACK, 5.0)]
        rewind_clock_history(clocks, history, 1)
        self.assertEqual(clocks[BLACK].used, 5.0)
        self.assertEqual(clocks[WHITE].used, 10.0)
        self.assertEqual(len(history), 1)


class TestSanityCheck(unittest.TestCase):

    def test_empty_board_is_clean(self):
        ok, lines = sanity_check(GameState())
        self.assertTrue(ok, lines)
        self.assertIn("OK", lines[0])

    def test_after_opening_stone(self):
        state = GameState()
        state.play_turn([B.parse_cell("E5")])
        ok, lines = sanity_check(state)
        self.assertTrue(ok, lines)
        self.assertIn("Black to move", lines[0])

    def test_detects_a_tampered_score(self):
        state = GameState()
        state.play_turn([B.parse_cell("E5")])
        state.largest[B.WHITE] = 99
        ok, lines = sanity_check(state)
        self.assertFalse(ok)
        self.assertTrue(any("largest" in line for line in lines))


if __name__ == "__main__":
    unittest.main()
