"""The rules of Catchup: turn sizes, the catch-up condition and the tie-break."""

import unittest

from catchup.board import BLACK, WHITE, parse_cell
from catchup.state import GameState, IllegalMove, WIN_SCORE
from tests.helpers import random_position


def cells(*names):
    return [parse_cell(n) for n in names]


class FixedGroups(GameState):
    """A finished position with the group sizes stated directly.

    Building a real board with an exact group profile is fiddly, and the
    tie-break only ever looks at the sorted sizes, so the sizes are injected.
    """

    __slots__ = ("_forced",)

    def __init__(self, white, black):
        super().__init__()
        self._forced = {WHITE: sorted(white, reverse=True),
                        BLACK: sorted(black, reverse=True)}
        for player, sizes in ((WHITE, white), (BLACK, black)):
            hist = self.size_n[player]
            for s in sizes:
                hist[s] += 1

    def group_sizes(self, player):
        return list(self._forced[player])


class TestTurnSizes(unittest.TestCase):

    def test_white_opens_with_exactly_one_stone(self):
        state = GameState()
        self.assertEqual(state.max_this_turn, 1)
        self.assertEqual(state.legal_turn_sizes(), [1])
        with self.assertRaises(IllegalMove):
            state.play_turn(cells("E5", "D4"))

    def test_both_scores_start_at_one(self):
        state = GameState()
        self.assertEqual(state.score(WHITE), 1)
        self.assertEqual(state.score(BLACK), 1)

    def test_black_never_gets_three_on_the_first_turn(self):
        # A remark in the rules description: White's single opening stone does not
        # count as an increase, because scores already start at 1.
        for opening in ("E5", "A1", "I9"):
            state = GameState()
            state.play_turn(cells(opening))
            self.assertEqual(state.score(WHITE), 1)
            self.assertFalse(state.score_increased[WHITE])
            self.assertEqual(state.max_this_turn, 2, opening)

    def test_a_turn_must_place_at_least_one_stone(self):
        state = GameState()
        with self.assertRaises(IllegalMove):
            state.play_turn([])

    def test_placements_are_capped_by_the_empty_cells_left(self):
        state = GameState()
        while state.empty_count > 1:
            state.play_turn([next(c for c in range(61) if not state.colour[c])])
        self.assertEqual(state.empty_count, 1)
        self.assertEqual(state.max_this_turn, 1)
        with self.assertRaises(IllegalMove):
            state.play_turn(cells("A1", "B1"))


class TestCatchUpCondition(unittest.TestCase):
    """Rule 2: three stones need an increase *and* a score at least as large."""

    def opening(self):
        state = GameState()
        state.play_turn(cells("E5"))
        return state

    def test_increase_and_not_behind_grants_three(self):
        state = self.opening()
        state.play_turn(cells("A1", "B1"))         # Black's score 1 -> 2
        self.assertTrue(state.score_increased[BLACK])
        self.assertEqual(state.score(BLACK), 2)
        self.assertGreaterEqual(state.score(BLACK), state.score(WHITE))
        self.assertEqual(state.max_this_turn, 3)
        self.assertTrue(state.allows_three)

    def test_no_increase_means_only_two(self):
        state = self.opening()
        state.play_turn(cells("A1", "I9"))         # two lone stones, score stays 1
        self.assertFalse(state.score_increased[BLACK])
        self.assertEqual(state.score(BLACK), 1)
        self.assertEqual(state.max_this_turn, 2)

    def test_increase_while_still_behind_means_only_two(self):
        state = GameState()
        state.play_turn(cells("E5"))
        state.play_turn(cells("A1"))
        state.play_turn(cells("E4", "F5"))         # White builds to 3
        state.play_turn(cells("I9"))
        state.play_turn(cells("F6", "D4"))         # White to 5
        self.assertEqual(state.score(WHITE), 5)
        state.play_turn(cells("A2", "B2"))         # Black rises 1 -> 3
        self.assertTrue(state.score_increased[BLACK])
        self.assertEqual(state.score(BLACK), 3)
        self.assertLess(state.score(BLACK), state.score(WHITE))
        self.assertEqual(state.max_this_turn, 2)   # increase alone is not enough

    def test_equal_scores_are_enough(self):
        state = GameState()
        state.play_turn(cells("E5"))
        state.play_turn(cells("A1", "B1"))         # Black 2, White 1 -> White gets 3
        state.play_turn(cells("E4", "F5"))         # White 1 -> 3
        # Black's turn: White increased and 3 >= 2, so Black may place three.
        self.assertTrue(state.score_increased[WHITE])
        self.assertEqual(state.max_this_turn, 3)

    def test_permission_is_part_of_the_hash(self):
        # Two different move orders can reach the very same board with the same
        # side to move and still differ in whether three stones are allowed,
        # because the permission depends on the previous turn rather than on the
        # stones.  The Zobrist key must therefore tell the two apart, or the
        # transposition table would hand back a value for the wrong position.
        denying = GameState()
        denying.play_turn(cells("E5"))
        denying.play_turn(cells("A1", "B1"))   # Black 1 -> 2
        denying.play_turn(cells("F5"))         # White 1 -> 2
        denying.play_turn(cells("I9"))         # a lone stone, Black stays at 2

        allowing = GameState()
        allowing.play_turn(cells("E5"))
        allowing.play_turn(cells("A1", "I9"))  # split, Black stays at 1
        allowing.play_turn(cells("F5"))        # White 1 -> 2
        allowing.play_turn(cells("B1"))        # joins A1, Black 1 -> 2

        # Same stones, same side to move.
        self.assertEqual(denying.colour, allowing.colour)
        self.assertEqual(denying.to_move, allowing.to_move, WHITE)
        self.assertEqual(denying.score(WHITE), allowing.score(WHITE))
        self.assertEqual(denying.score(BLACK), allowing.score(BLACK))

        # But different permissions, and so different keys.
        self.assertFalse(denying.allows_three)
        self.assertEqual(denying.max_this_turn, 2)
        self.assertTrue(allowing.allows_three)
        self.assertEqual(allowing.max_this_turn, 3)
        self.assertNotEqual(denying.zobrist, allowing.zobrist)


class TestTieBreak(unittest.TestCase):
    """Rules 3 and 4."""

    def test_largest_group_decides(self):
        self.assertEqual(FixedGroups([10], [9]).compare_final(), 1)
        self.assertEqual(FixedGroups([9], [10]).compare_final(), -1)

    def test_example_from_the_rules(self):
        # {6,5,5,3} beats {6,5,4,3}: the second 5 is used as the third group.
        self.assertEqual(
            FixedGroups([6, 5, 5, 3], [6, 5, 4, 3]).compare_final(), 1)
        self.assertEqual(
            FixedGroups([6, 5, 4, 3], [6, 5, 5, 3]).compare_final(), -1)

    def test_identical_profiles_draw(self):
        self.assertEqual(FixedGroups([6, 5, 4, 3], [6, 5, 4, 3]).compare_final(), 0)
        self.assertEqual(FixedGroups([1] * 30, [1] * 30).compare_final(), 0)

    def test_a_surviving_group_breaks_the_tie(self):
        # Running out of groups counts as size 0 from then on.
        self.assertEqual(FixedGroups([6, 5], [6, 5, 1]).compare_final(), -1)
        self.assertEqual(FixedGroups([6, 5, 1], [6, 5]).compare_final(), 1)

    def test_deep_tie_break(self):
        self.assertEqual(FixedGroups([7, 2], [7, 1, 1, 1]).compare_final(), 1)
        self.assertEqual(FixedGroups([7, 1, 1, 1], [7, 2]).compare_final(), -1)

    def test_terminal_score_is_side_relative(self):
        won = FixedGroups([8], [7])
        self.assertEqual(won.terminal_score(WHITE), WIN_SCORE)
        self.assertEqual(won.terminal_score(BLACK), -WIN_SCORE)
        drawn = FixedGroups([8], [8])
        self.assertEqual(drawn.terminal_score(WHITE), 0)
        self.assertEqual(drawn.terminal_score(BLACK), 0)


class TestGameEnd(unittest.TestCase):

    def test_board_fills_exactly(self):
        state = random_position(seed=11, stones=None, stop_before_end=False)
        self.assertTrue(state.is_over)
        self.assertEqual(state.empty_count, 0)
        self.assertEqual(state.nstones[WHITE] + state.nstones[BLACK], 61)

    def test_every_cell_belongs_to_exactly_one_group(self):
        state = random_position(seed=12, stones=None, stop_before_end=False)
        total = sum(state.group_sizes(WHITE)) + sum(state.group_sizes(BLACK))
        self.assertEqual(total, 61)


class TestMoveValidation(unittest.TestCase):

    def test_rejects_occupied_and_repeated_cells(self):
        state = GameState()
        state.play_turn(cells("E5"))
        with self.assertRaises(IllegalMove):
            state.play_turn(cells("E5"))
        with self.assertRaises(IllegalMove):
            state.play_turn(cells("A1", "A1"))

    def test_rejects_indices_off_the_board(self):
        state = GameState()
        with self.assertRaises(IllegalMove):
            state.play_turn([61])
        with self.assertRaises(IllegalMove):
            state.play_turn([-1])


if __name__ == "__main__":
    unittest.main()
