"""GameState wrapper around libcatchup."""

from ctypes import byref, c_char, c_int, cast, POINTER

from ._lib import CatchupStateData, lib

WIN_SCORE = 10_000_000


class IllegalMove(Exception):
    """Raised when a caller tries to make a move the rules do not allow."""


def _c_ints(values):
    arr = (c_int * len(values))(*values)
    return arr


class GameState:
    def __init__(self, _ptr=None):
        self._ptr = _ptr or lib.catchup_state_new()
        if not self._ptr:
            raise MemoryError("catchup_state_new failed")
        self._own = _ptr is None
        self._d = CatchupStateData()
        self._pull()

    def __del__(self):
        ptr = getattr(self, "_ptr", None)
        own = getattr(self, "_own", False)
        if ptr and own:
            lib.catchup_state_free(ptr)

    def _pull(self):
        lib.catchup_state_get(self._ptr, byref(self._d))
        d = self._d
        self.colour = bytearray(d.colour)
        self.bb = [int(d.bb[0]), int(d.bb[1])]
        self.adjacency = [bytearray(d.adjacency[0]), bytearray(d.adjacency[1])]
        self.touch = [int(d.touch[0]), int(d.touch[1])]
        self.join = [int(d.join[0]), int(d.join[1])]
        self.empty_count = d.empty_count
        self.parent = list(d.parent)
        self.size = list(d.size)
        self.size_n = [list(d.size_n[0]), list(d.size_n[1])]
        self.lib_root = [int(d.lib_root[i]) for i in range(61)]
        self.largest = [d.largest[0], d.largest[1]]
        self.second = [d.second[0], d.second[1]]
        self.largest_root = [d.largest_root[0], d.largest_root[1]]
        self.sumsq = [d.sumsq[0], d.sumsq[1]]
        self.nstones = [d.nstones[0], d.nstones[1]]
        self.centrality = [d.centrality[0], d.centrality[1]]
        self.frontier = [d.frontier[0], d.frontier[1]]
        self.to_move = d.to_move
        self.turn_index = d.turn_index
        self.placed_this_turn = d.placed_this_turn
        self.max_this_turn = d.max_this_turn
        self.score_at_turn_start = d.score_at_turn_start
        self.score_increased = [bool(d.score_increased[0]), bool(d.score_increased[1])]
        self.allows_three = bool(d.allows_three)
        self.zobrist = int(d.zobrist)
        self._undo_n = d.undo_n
        self._turn_undo_n = d.turn_undo_n
        self._lib_undo_n = d.lib_undo_n

    def _push(self):
        d = self._d
        for i in range(61):
            d.colour[i] = self.colour[i]
            d.parent[i] = self.parent[i]
            d.size[i] = self.size[i]
            d.lib_root[i] = self.lib_root[i]
            d.adjacency[0][i] = self.adjacency[0][i]
            d.adjacency[1][i] = self.adjacency[1][i]
        d.bb[0], d.bb[1] = self.bb[0], self.bb[1]
        d.touch[0], d.touch[1] = self.touch[0], self.touch[1]
        d.join[0], d.join[1] = self.join[0], self.join[1]
        d.empty_count = self.empty_count
        for p in range(2):
            for s in range(62):
                d.size_n[p][s] = self.size_n[p][s]
            d.largest[p] = self.largest[p]
            d.second[p] = self.second[p]
            d.largest_root[p] = self.largest_root[p]
            d.sumsq[p] = self.sumsq[p]
            d.nstones[p] = self.nstones[p]
            d.centrality[p] = self.centrality[p]
            d.frontier[p] = self.frontier[p]
            d.score_increased[p] = 1 if self.score_increased[p] else 0
        d.to_move = self.to_move
        d.turn_index = self.turn_index
        d.placed_this_turn = self.placed_this_turn
        d.max_this_turn = self.max_this_turn
        d.score_at_turn_start = self.score_at_turn_start
        d.allows_three = 1 if self.allows_three else 0
        d.zobrist = self.zobrist
        lib.catchup_state_set(self._ptr, byref(d))

    def find(self, cell):
        return lib.catchup_state_find(self._ptr, cell)

    def largest_lib_mask(self, player):
        return int(lib.catchup_state_largest_lib_mask(self._ptr, player))

    def largest_liberties(self, player):
        return lib.catchup_state_largest_liberties(self._ptr, player)

    def max_reachable(self, player):
        return lib.catchup_state_max_reachable(self._ptr, player)

    def largest_reachable(self, player):
        return lib.catchup_state_largest_reachable(self._ptr, player)

    def place(self, cell):
        self._push()
        if lib.catchup_state_place(self._ptr, cell) != 0:
            raise IllegalMove("cannot place on cell %r" % (cell,))
        self._pull()

    def unplace(self):
        if lib.catchup_state_unplace(self._ptr) != 0:
            raise RuntimeError("no placement to undo")
        self._pull()

    def end_turn(self):
        self._push()
        if lib.catchup_state_end_turn(self._ptr) != 0:
            raise RuntimeError("catchup_state_end_turn failed")
        self._pull()

    def unend_turn(self):
        if lib.catchup_state_unend_turn(self._ptr) != 0:
            raise RuntimeError("no turn to undo")
        self._pull()

    @property
    def is_over(self):
        return bool(lib.catchup_state_is_over(self._ptr))

    def placements_left(self):
        return lib.catchup_state_placements_left(self._ptr)

    def score(self, player):
        return lib.catchup_state_score(self._ptr, player)

    def group_sizes(self, player):
        buf = (c_int * 61)()
        n = lib.catchup_state_group_sizes(self._ptr, player, buf, 61)
        return [buf[i] for i in range(n)]

    def compare_final(self):
        self._push()
        return lib.catchup_state_compare_final(self._ptr)

    def terminal_score(self, player):
        self._push()
        return lib.catchup_state_terminal_score(self._ptr, player)

    def legal_turn_sizes(self):
        buf = (c_int * 4)()
        n = lib.catchup_state_legal_turn_sizes(self._ptr, buf, 4)
        return [buf[i] for i in range(n)]

    def play_turn(self, cells):
        cells = list(cells)
        arr = _c_ints(cells) if cells else (c_int * 1)()
        err = (c_char * 256)()
        rc = lib.catchup_state_play_turn(
            self._ptr, cast(arr, POINTER(c_int)) if cells else None,
            len(cells), err, 256)
        if rc != 0:
            raise IllegalMove(err.value.decode("utf-8", "replace"))
        self._pull()

    def undo_turn(self, stone_count):
        if lib.catchup_state_undo_turn(self._ptr, stone_count) != 0:
            raise RuntimeError("cannot undo a turn of %r stone(s)" % (stone_count,))
        self._pull()

    def clone(self):
        self._push()
        ptr = lib.catchup_state_clone(self._ptr)
        other = GameState.__new__(GameState)
        other._ptr = ptr
        other._own = True
        other._d = CatchupStateData()
        other._pull()
        return other

    def __repr__(self):
        return "<GameState turn=%d %s to move, %d/%d placed, %d empty>" % (
            self.turn_index,
            "White" if self.to_move == 0 else "Black",
            self.placed_this_turn, self.max_this_turn, self.empty_count,
        )
