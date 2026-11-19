"""Console rendering and the human-move parser."""

import unittest

from catchup import board as B
from catchup.state import GameState
from catchup.ui import (
    format_result,
    format_turn,
    parse_turn,
    render,
)


class TestRender(unittest.TestCase):

    def test_empty_board_shows_every_cell_letter_and_the_row_numbers(self):
        picture = render(GameState())
        lines = picture.splitlines()
        self.assertEqual(len(lines), 9)
        # Row labels 9 through 1, top to bottom, matching Figure 2.
        labels = [int(line.split()[0]) for line in lines]
        self.assertEqual(labels, [9, 8, 7, 6, 5, 4, 3, 2, 1])
        # Empty cells show their column letter in lower case.
        self.assertIn("e f g h i", lines[0])   # row 9
        self.assertIn("a", lines[4])           # A5 on the equator
        self.assertIn("a b c d e", lines[-1])  # row 1

    def test_stones_replace_the_letter_glyphs(self):
        state = GameState()
        state.play_turn([B.parse_cell("E5")])
        picture = render(state)
        equator = [line for line in picture.splitlines()
                   if line.split()[0] == "5"][0]
        self.assertIn("O", equator)
        self.assertEqual(equator.count("O"), 1)


class TestParseTurn(unittest.TestCase):

    def test_accepts_one_to_three_cells(self):
        state = GameState()
        self.assertEqual(parse_turn("e5", state), [B.parse_cell("E5")])
        state.play_turn([B.parse_cell("E5")])
        self.assertEqual(
            parse_turn("A1, b1", state),
            [B.parse_cell("A1"), B.parse_cell("B1")],
        )

    def test_rejects_occupied_unknown_and_too_many(self):
        state = GameState()
        with self.assertRaises(ValueError):
            parse_turn("E5 D4", state)          # White may only place one
        with self.assertRaises(ValueError):
            parse_turn("Z0", state)
        state.play_turn([B.parse_cell("E5")])
        with self.assertRaises(ValueError):
            parse_turn("E5", state)
        with self.assertRaises(ValueError):
            parse_turn("A1 A1", state)
        with self.assertRaises(ValueError):
            parse_turn("", state)


class TestFormatting(unittest.TestCase):

    def test_format_turn(self):
        self.assertEqual(format_turn((B.parse_cell("E5"), B.parse_cell("D4"))),
                         "E5 D4")

    def test_format_result_uses_the_rules_example(self):
        from tests.test_rules import FixedGroups
        state = FixedGroups([6, 5, 5, 3], [6, 5, 4, 3])
        text = format_result(state)
        self.assertIn("White wins", text)
        self.assertIn("third largest", text)


if __name__ == "__main__":
    unittest.main()
