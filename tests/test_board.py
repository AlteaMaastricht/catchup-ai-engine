"""Geometry of the hexagonal board and the A1-I9 coordinate system."""

import unittest
from collections import Counter

from catchup import board as B


class TestGeometry(unittest.TestCase):

    def test_cell_count(self):
        self.assertEqual(len(B.CELL_COORDS), 61)
        self.assertEqual(len(set(B.CELL_NAMES)), 61)

    def test_row_lengths_match_the_figure(self):
        lengths = [len(cells) for _number, _indent, cells in B.DISPLAY_ROWS]
        self.assertEqual(lengths, [5, 6, 7, 8, 9, 8, 7, 6, 5])

    def test_row_contents_match_the_figure(self):
        rows = {number: [B.CELL_NAMES[c] for c in cells]
                for number, _indent, cells in B.DISPLAY_ROWS}
        self.assertEqual(rows[1], ["A1", "B1", "C1", "D1", "E1"])
        self.assertEqual(rows[5], ["A5", "B5", "C5", "D5", "E5",
                                   "F5", "G5", "H5", "I5"])
        self.assertEqual(rows[6], ["B6", "C6", "D6", "E6",
                                   "F6", "G6", "H6", "I6"])
        self.assertEqual(rows[9], ["E9", "F9", "G9", "H9", "I9"])

    def test_corner_and_centre_names_exist(self):
        for name in ("A1", "E1", "A5", "E5", "I5", "E9", "I9"):
            self.assertIn(name, B.NAME_TO_INDEX)
        # Cells outside the hexagon must not be addressable.
        for name in ("A6", "A9", "I1", "I4", "D9", "F1"):
            self.assertNotIn(name, B.NAME_TO_INDEX)


class TestAdjacency(unittest.TestCase):

    def neighbours_of(self, name):
        return sorted(B.CELL_NAMES[n]
                      for n in B.NEIGHBOURS[B.NAME_TO_INDEX[name]])

    def test_centre_has_six_neighbours(self):
        self.assertEqual(self.neighbours_of("E5"),
                         ["D4", "D5", "E4", "E6", "F5", "F6"])

    def test_reference_cell_from_the_figure(self):
        # Read straight off the board diagram in the rules description.
        self.assertEqual(self.neighbours_of("C5"),
                         ["B4", "B5", "C4", "C6", "D5", "D6"])

    def test_corners_have_three_neighbours(self):
        self.assertEqual(self.neighbours_of("A1"), ["A2", "B1", "B2"])
        self.assertEqual(self.neighbours_of("I9"), ["H8", "H9", "I8"])
        self.assertEqual(self.neighbours_of("A5"), ["A4", "B5", "B6"])

    def test_adjacency_is_symmetric(self):
        for cell in range(B.NUM_CELLS):
            for neighbour in B.NEIGHBOURS[cell]:
                self.assertIn(cell, B.NEIGHBOURS[neighbour])

    def test_degree_distribution(self):
        # Six corners of degree 3, eighteen edge cells of degree 4, the rest 6.
        self.assertEqual(dict(Counter(B.NEIGHBOUR_COUNT)), {3: 6, 4: 18, 6: 37})

    def test_neighbour_masks_match_the_adjacency_lists(self):
        for cell in range(B.NUM_CELLS):
            mask = 0
            for neighbour in B.NEIGHBOURS[cell]:
                mask |= 1 << neighbour
            self.assertEqual(B.NEIGHBOUR_MASK[cell], mask)
            self.assertEqual(bin(mask).count("1"), B.NEIGHBOUR_COUNT[cell])


class TestTables(unittest.TestCase):

    def test_ring_sizes(self):
        self.assertEqual(dict(Counter(B.RING)), {0: 1, 1: 6, 2: 12, 3: 18, 4: 24})

    def test_centrality_peaks_at_the_centre(self):
        best = max(range(B.NUM_CELLS), key=lambda c: B.CENTRALITY[c])
        self.assertEqual(B.CELL_NAMES[best], "E5")

    def test_static_order_is_a_permutation_from_the_centre_out(self):
        self.assertEqual(sorted(B.STATIC_ORDER), list(range(B.NUM_CELLS)))
        self.assertEqual(B.CELL_NAMES[B.STATIC_ORDER[0]], "E5")
        weights = [B.CENTRALITY[c] for c in B.STATIC_ORDER]
        self.assertEqual(weights, sorted(weights, reverse=True))
        self.assertEqual(
            [B.STATIC_RANK[c] for c in B.STATIC_ORDER],
            list(range(B.NUM_CELLS)))
        self.assertEqual(B.FULL_MASK.bit_count(), B.NUM_CELLS)

    def test_index_mask_from_matches_cell_lower_bounds(self):
        self.assertEqual(len(B.INDEX_MASK_FROM), B.NUM_CELLS + 1)
        self.assertEqual(B.INDEX_MASK_FROM[0], B.FULL_MASK)
        self.assertEqual(B.INDEX_MASK_FROM[B.NUM_CELLS], 0)
        for lower in (0, 1, 17, 40, 60, 61):
            mask = B.INDEX_MASK_FROM[lower]
            for cell in range(B.NUM_CELLS):
                bit = (mask >> cell) & 1
                self.assertEqual(bit, 1 if cell >= lower else 0,
                                 "bit %d for lower %d" % (cell, lower))

    def test_zobrist_keys_are_distinct(self):
        keys = list(B.ZOBRIST_CELL[0]) + list(B.ZOBRIST_CELL[1])
        keys += [B.ZOBRIST_SIDE, B.ZOBRIST_ALLOWS_THREE]
        keys += list(B.ZOBRIST_PLACED)
        self.assertEqual(len(set(keys)), len(keys))


class TestParsing(unittest.TestCase):

    def test_round_trip(self):
        for cell in range(B.NUM_CELLS):
            self.assertEqual(B.parse_cell(B.CELL_NAMES[cell]), cell)

    def test_case_and_whitespace_are_forgiven(self):
        self.assertEqual(B.parse_cell(" e5 "), B.NAME_TO_INDEX["E5"])

    def test_rejects_cells_off_the_board(self):
        for bad in ("A6", "Z1", "E10", "", "5E"):
            with self.assertRaises(ValueError):
                B.parse_cell(bad)


if __name__ == "__main__":
    unittest.main()
