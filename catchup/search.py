"""Engine / Clock wrappers around libcatchup."""

from ctypes import byref, c_int

from .board import CELL_NAMES, NUM_CELLS
from ._lib import CatchupEngineConfig, CatchupSearchStats, lib
from .state import GameState

INFINITY = 1 << 30
END_TURN = -1
EXACT = 0
LOWER_BOUND = 1
UPPER_BOUND = 2
TT_BITS = 20  # must match catchup::TT_BITS in src/search.hpp
TT_BUCKETS = 1 << TT_BITS
LATE_MERGE_EMPTY = 10
KIND_MERGE = 0
KIND_BLOCK = 1
KIND_EXPAND = 2
KIND_HARRASS = 3
KIND_QUIET = 4


class TimeoutException(Exception):
    """Raised deep in the search when the per-turn deadline passes."""


class SearchStats:
    def __init__(self):
        self.nodes = 0
        self.depth = 0
        self.value = 0
        self.elapsed = 0.0
        self.turn = ()
        self.completed = 0

    @property
    def nps(self):
        return self.nodes / self.elapsed if self.elapsed > 0 else 0.0

    def describe(self):
        return "depth %d  value %+d  %d nodes in %.2fs (%.0f n/s)  turn %s" % (
            self.depth, self.value, self.nodes, self.elapsed, self.nps,
            " ".join(CELL_NAMES[c] for c in self.turn),
        )


class Clock:
    def __init__(self, total=600.0, reserve=8.0):
        self._ptr = lib.catchup_clock_new(float(total), float(reserve))
        if not self._ptr:
            raise MemoryError("catchup_clock_new failed")
        self.total = float(total)
        self.reserve = float(reserve)

    def __del__(self):
        ptr = getattr(self, "_ptr", None)
        if ptr:
            lib.catchup_clock_free(ptr)

    @property
    def used(self):
        return lib.catchup_clock_get_used(self._ptr)

    @used.setter
    def used(self, value):
        lib.catchup_clock_set_used(self._ptr, float(value))

    def remaining(self):
        return lib.catchup_clock_remaining(self._ptr)

    def allocate(self, state):
        state._push()
        return lib.catchup_clock_allocate(self._ptr, state._ptr)

    def consume(self, seconds):
        lib.catchup_clock_consume(self._ptr, float(seconds))

    def note_own_stones(self, stones):
        lib.catchup_clock_note_own(self._ptr, int(stones))

    def note_reply_stones(self, stones):
        lib.catchup_clock_note_reply(self._ptr, int(stones))

    def undo_note(self):
        lib.catchup_clock_undo_note(self._ptr)


class Engine:
    def __init__(
        self,
        use_tt=True,
        use_pvs=True,
        use_killers=True,
        use_history=True,
        use_aspiration=True,
        use_lmr=True,
        use_opening_book=True,
        max_depth=64,
        aspiration_delta=120,
        w_largest=100,
        w_sumsq=2,
        w_frontier=1,
        w_centre=2,
        w_tempo=0,
        w_pending=25,
        w_liberty=8,
        w_stones=20,
        w_second=40,
        w_join=4,
        w_crit=16,
        w_third=25,
        w_fourth=8,
        use_rfp=True,
        use_lmp=True,
        use_iid=False,
        w_gift=80,
        w_reachable=10,
        tt_bits=TT_BITS,
        use_countermove=True,
        use_stable_stop=True,
    ):
        cfg = CatchupEngineConfig(
            1 if use_tt else 0,
            1 if use_pvs else 0,
            1 if use_killers else 0,
            1 if use_history else 0,
            1 if use_aspiration else 0,
            1 if use_lmr else 0,
            1 if use_opening_book else 0,
            max_depth,
            aspiration_delta,
            w_largest, w_sumsq, w_frontier, w_centre, w_tempo,
            w_pending, w_liberty, w_stones, w_second, w_join,
            w_crit, w_third, w_fourth,
            1 if use_rfp else 0,
            1 if use_lmp else 0,
            1 if use_iid else 0,
            w_gift,
            w_reachable,
            int(tt_bits),
            1 if use_countermove else 0,
            1 if use_stable_stop else 0,
        )
        self._ptr = lib.catchup_engine_new(byref(cfg))
        if not self._ptr:
            raise MemoryError("catchup_engine_new failed")
        self.settings = dict(
            use_tt=bool(use_tt), use_pvs=bool(use_pvs),
            use_killers=bool(use_killers), use_history=bool(use_history),
            use_aspiration=bool(use_aspiration), use_lmr=bool(use_lmr),
            use_opening_book=bool(use_opening_book), max_depth=int(max_depth),
            aspiration_delta=int(aspiration_delta),
            w_largest=int(w_largest), w_sumsq=int(w_sumsq),
            w_frontier=int(w_frontier), w_centre=int(w_centre),
            w_tempo=int(w_tempo), w_pending=int(w_pending),
            w_liberty=int(w_liberty), w_stones=int(w_stones),
            w_second=int(w_second), w_join=int(w_join),
            w_crit=int(w_crit), w_third=int(w_third), w_fourth=int(w_fourth),
            use_rfp=bool(use_rfp), use_lmp=bool(use_lmp), use_iid=bool(use_iid),
            w_gift=int(w_gift), w_reachable=int(w_reachable),
            tt_bits=int(tt_bits),
            use_countermove=bool(use_countermove),
            use_stable_stop=bool(use_stable_stop),
        )
        self.st = None
        # Placeholders so existing tests that poke these attributes don't crash
        # if they still import the Python Engine. Heavy search tests live in C++.
        self.history = [[0] * NUM_CELLS, [0] * NUM_CELLS]
        self.killer0 = [-1] * 160
        self.killer1 = [-1] * 160
        self.move_buf = [[0] * 64 for _ in range(160)]
        self.move_kind = [[0] * 64 for _ in range(160)]
        self.move_len = [0] * 160

    def __del__(self):
        ptr = getattr(self, "_ptr", None)
        if ptr:
            lib.catchup_engine_free(ptr)

    @property
    def deadline(self):
        return lib.catchup_engine_get_deadline(self._ptr)

    @deadline.setter
    def deadline(self, value):
        lib.catchup_engine_set_deadline(self._ptr, float(value))

    def new_game(self):
        lib.catchup_engine_new_game(self._ptr)
        self.history = [[0] * NUM_CELLS, [0] * NUM_CELLS]
        self.killer0 = [-1] * 160
        self.killer1 = [-1] * 160

    def find_turn(self, state, time_limit, report=None):
        state._push()
        stats_c = CatchupSearchStats()
        rc = lib.catchup_engine_find_turn(self._ptr, state._ptr, float(time_limit),
                                          byref(stats_c))
        state._pull()
        if rc != 0:
            raise RuntimeError("catchup_engine_find_turn failed")
        stats = SearchStats()
        stats.nodes = stats_c.nodes
        stats.depth = stats_c.depth
        stats.value = stats_c.value
        stats.completed = stats_c.completed
        stats.elapsed = stats_c.elapsed
        stats.turn = tuple(stats_c.turn[i] for i in range(stats_c.turn_n))
        if report is not None:
            report(stats.completed, stats.value, stats.turn, stats.nodes, stats.elapsed)
        return stats.turn, stats

    def evaluate(self, state, placements_left=None):
        state._push()
        if placements_left is None:
            placements_left = state.placements_left()
        return lib.catchup_engine_evaluate(self._ptr, state._ptr, int(placements_left))

    def set_eval_weights(self, weights):
        buf = (c_int * 15)(*[int(w) for w in weights])
        lib.catchup_engine_set_eval_weights(self._ptr, buf)
        names = (
            "w_largest", "w_sumsq", "w_frontier", "w_centre", "w_tempo",
            "w_pending", "w_liberty", "w_stones", "w_second", "w_join",
            "w_crit", "w_third", "w_fourth", "w_gift", "w_reachable",
        )
        for name, value in zip(names, weights):
            self.settings[name] = int(value)

    def get_eval_weights(self):
        buf = (c_int * 15)()
        lib.catchup_engine_get_eval_weights(self._ptr, buf)
        return [int(buf[i]) for i in range(15)]
