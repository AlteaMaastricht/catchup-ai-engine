"""The incremental machinery, checked against recomputation from scratch.

Everything the evaluation reads is maintained inside ``place`` and reversed by
``unplace``.  These tests replay random games and, after every single stone,
compare the accumulators with a slow reference that rebuilds them from the raw
board, then unwind the whole game and require the state to be pristine again.
"""

import unittest

from catchup import board as B
from catchup.state import GameState
from tests.helpers import (
    groups_by_flood_fill,
    random_playout,
    random_position,
    recompute,
    recompute_zobrist,
)


class AccumulatorMixin:

    def assert_consistent(self, state, label=""):
        for player in (B.WHITE, B.BLACK):
            expected = recompute(state, player)
            self.assertEqual(state.largest[player], expected["largest"],
                             "largest %s" % label)
            self.assertEqual(state.sumsq[player], expected["sumsq"],
                             "sumsq %s" % label)
            self.assertEqual(state.nstones[player], expected["nstones"],
                             "nstones %s" % label)
            self.assertEqual(state.centrality[player], expected["centrality"],
                             "centrality %s" % label)
            self.assertEqual(state.frontier[player], expected["frontier"],
                             "frontier %s" % label)
            self.assertEqual(list(state.adjacency[player]), expected["adjacency"],
                             "adjacency %s" % label)
            self.assertEqual(state.group_sizes(player), expected["groups"],
                             "groups %s" % label)
            self.assertEqual(state.second[player], expected["second"],
                             "second %s" % label)
            self.assertEqual(state.largest_liberties(player), expected["liberties"],
                             "liberties %s" % label)
            touch = 0
            join = 0
            adj = state.adjacency[player]
            for cell in range(B.NUM_CELLS):
                a = adj[cell]
                if a:
                    touch |= 1 << cell
                    if a >= 2:
                        join |= 1 << cell
            self.assertEqual(state.touch[player], touch, "touch %s" % label)
            self.assertEqual(state.join[player], join, "join %s" % label)
        self.assertEqual(state.zobrist, recompute_zobrist(state),
                         "zobrist %s" % label)
        self.assertEqual(
            state.empty_count,
            B.NUM_CELLS - state.nstones[B.WHITE] - state.nstones[B.BLACK])
        for player in (B.WHITE, B.BLACK):
            expected_bb = 0
            stone = B.STONE[player]
            for cell in range(B.NUM_CELLS):
                if state.colour[cell] == stone:
                    expected_bb |= 1 << cell
            self.assertEqual(state.bb[player], expected_bb, "bitboard %s" % label)


class TestDisjointSet(AccumulatorMixin, unittest.TestCase):

    def test_matches_flood_fill_over_random_games(self):
        for seed in range(6):
            state, _history = random_playout(seed)
            for player in (B.WHITE, B.BLACK):
                self.assertEqual(state.group_sizes(player),
                                 groups_by_flood_fill(state, player),
                                 "seed %d player %d" % (seed, player))

    def test_roots_are_well_defined(self):
        # Without path compression a root is exactly a self-parent, and every
        # cell must reach one.
        state = random_position(seed=3, stones=40)
        for cell in range(B.NUM_CELLS):
            if state.colour[cell]:
                root = state.find(cell)
                self.assertEqual(state.parent[root], root)
                self.assertEqual(state.colour[root], state.colour[cell])

    def test_group_sizes_sum_to_stone_counts(self):
        state = random_position(seed=4, stones=45)
        for player in (B.WHITE, B.BLACK):
            self.assertEqual(sum(state.group_sizes(player)), state.nstones[player])

    def test_a_single_stone_can_merge_three_groups(self):
        # The six neighbours of a cell form a ring, so at most three of them are
        # pairwise non-adjacent.  Fill those three, then join them.
        state = GameState()
        centre = B.NAME_TO_INDEX["E5"]
        # Greedily take neighbours of E5 that do not touch one another.
        chosen = []
        for candidate in B.NEIGHBOURS[centre]:
            if all(candidate not in B.NEIGHBOURS[c] for c in chosen):
                chosen.append(candidate)
        self.assertEqual(len(chosen), 3)

        state.to_move = B.WHITE
        for cell in chosen:
            state.place(cell)
        self.assertEqual(sorted(state.group_sizes(B.WHITE)), [1, 1, 1])
        state.place(centre)
        self.assertEqual(state.group_sizes(B.WHITE), [4])
        self.assertEqual(state.largest[B.WHITE], 4)


def _lex_from_sizes(white, black):
    for i in range(max(len(white), len(black))):
        w = white[i] if i < len(white) else 0
        b = black[i] if i < len(black) else 0
        if w != b:
            return 1 if w > b else -1
    return 0


class TestCompareFinal(unittest.TestCase):

    def test_histogram_matches_sorted_group_vectors(self):
        for seed, stones in ((0, 20), (1, 40), (7, 55)):
            state = random_position(seed=seed, stones=stones)
            expected = _lex_from_sizes(
                state.group_sizes(B.WHITE), state.group_sizes(B.BLACK))
            self.assertEqual(state.compare_final(), expected,
                             "seed %s stones %s" % (seed, stones))
        state, _history = random_playout(3)
        expected = _lex_from_sizes(
            state.group_sizes(B.WHITE), state.group_sizes(B.BLACK))
        self.assertEqual(state.compare_final(), expected)

    def test_extra_trailing_group_breaks_the_tie(self):
        state = GameState()
        state.to_move = B.WHITE
        for name in ("E5", "E4", "A1"):
            state.place(B.NAME_TO_INDEX[name])
        state.to_move = B.BLACK
        for name in ("I9", "I8"):
            state.place(B.NAME_TO_INDEX[name])
        white = state.group_sizes(B.WHITE)
        black = state.group_sizes(B.BLACK)
        self.assertEqual(state.compare_final(), _lex_from_sizes(white, black))
        self.assertEqual(state.compare_final(), 1)


class TestIncrementalAccumulators(AccumulatorMixin, unittest.TestCase):

    def test_consistent_after_every_stone(self):
        state = GameState()
        import random
        rng = random.Random(777)
        while not state.is_over:
            count = rng.randint(1, state.max_this_turn)
            empty = [c for c in range(B.NUM_CELLS) if not state.colour[c]]
            for cell in rng.sample(empty, count):
                state.place(cell)
                self.assert_consistent(state, "after placing %s" % B.CELL_NAMES[cell])
            state.end_turn()
            self.assert_consistent(state, "after end_turn")

    def test_unwinding_restores_the_initial_state(self):
        fields = ("colour", "bb", "adjacency", "touch", "join", "empty_count", "parent", "size",
                  "size_n", "lib_root",
                  "largest", "second", "largest_root", "sumsq", "nstones",
                  "centrality", "frontier",
                  "to_move", "turn_index", "placed_this_turn", "max_this_turn",
                  "score_at_turn_start", "score_increased", "allows_three",
                  "zobrist", "_undo_n", "_turn_undo_n", "_lib_undo_n")
        for seed in range(4):
            state, history = random_playout(seed)
            for count in reversed(history):
                state.undo_turn(count)
                self.assert_consistent(state, "unwinding seed %d" % seed)
            fresh = GameState()
            for field in fields:
                self.assertEqual(getattr(state, field), getattr(fresh, field),
                                 "%s after unwinding seed %d" % (field, seed))

    def test_place_unplace_is_a_no_op(self):
        state = random_position(seed=9, stones=30)
        before = (bytes(state.colour), list(state.parent), list(state.size),
                  list(state.largest), list(state.second), list(state.sumsq),
                  list(state.frontier), list(state.centrality), list(state.lib_root),
                  state.zobrist, state.empty_count)
        for cell in range(B.NUM_CELLS):
            if state.colour[cell]:
                continue
            state.place(cell)
            state.unplace()
        after = (bytes(state.colour), list(state.parent), list(state.size),
                 list(state.largest), list(state.second), list(state.sumsq),
                 list(state.frontier), list(state.centrality), list(state.lib_root),
                 state.zobrist, state.empty_count)
        self.assertEqual(before, after)


class TestZobrist(unittest.TestCase):

    def test_incremental_key_matches_recomputation(self):
        for seed in range(4):
            state, _ = random_playout(seed)
            self.assertEqual(state.zobrist, recompute_zobrist(state))

    def test_transpositions_share_a_key(self):
        # The same stones reached in a different order, with the same side to
        # move and the same three-stone permission, must hash identically.
        from catchup.board import parse_cell as P
        first = GameState()
        first.play_turn([P("E5")])
        first.play_turn([P("A1"), P("I9")])

        second = GameState()
        second.play_turn([P("E5")])
        second.play_turn([P("I9"), P("A1")])

        self.assertEqual(first.zobrist, second.zobrist)
        self.assertEqual(first.allows_three, second.allows_three)


class TestClone(unittest.TestCase):

    def test_clone_is_independent(self):
        state = random_position(seed=13, stones=25)
        copy = state.clone()
        self.assertEqual(copy.zobrist, state.zobrist)
        cell = next(c for c in range(B.NUM_CELLS) if not copy.colour[c])
        copy.place(cell)
        self.assertNotEqual(copy.zobrist, state.zobrist)
        self.assertFalse(state.colour[cell])
        self.assertEqual(state.zobrist, recompute_zobrist(state))


if __name__ == "__main__":
    unittest.main()
