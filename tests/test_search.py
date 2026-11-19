"""Public search API (internals are tested in the C++ suite)."""

import unittest

from catchup import board as B
from catchup.search import Clock, Engine
from catchup.state import GameState
from tests.helpers import random_position


class TestOpeningAndLegality(unittest.TestCase):

    def test_opening_book_plays_the_centre(self):
        engine = Engine()
        turn, _stats = engine.find_turn(GameState(), 1.0)
        self.assertEqual(turn, (B.NAME_TO_INDEX["E5"],))

    def test_returned_turn_is_always_legal(self):
        engine = Engine(use_opening_book=False, max_depth=2)
        for stones, seed in ((0, 0), (8, 1), (20, 2)):
            state = random_position(seed=seed, stones=stones) if stones else GameState()
            if state.is_over:
                continue
            turn, _stats = engine.find_turn(state.clone(), 0.5)
            self.assertGreaterEqual(len(turn), 1)
            self.assertLessEqual(len(turn), state.max_this_turn)
            self.assertEqual(len(set(turn)), len(turn))
            for cell in turn:
                self.assertFalse(state.colour[cell], B.CELL_NAMES[cell])
            copy = state.clone()
            copy.play_turn(turn)
            for cell in turn:
                self.assertTrue(copy.colour[cell])


class TestClock(unittest.TestCase):

    def test_three_stone_turns_get_a_larger_clock_slice(self):
        state = GameState()
        state.play_turn([B.parse_cell("E5")])
        state.play_turn([B.parse_cell("A1"), B.parse_cell("B1")])
        clock = Clock(600.0)
        state.max_this_turn = 2
        two = clock.allocate(state)
        state.max_this_turn = 3
        three = clock.allocate(state)
        self.assertGreater(three, two)


if __name__ == "__main__":
    unittest.main()
