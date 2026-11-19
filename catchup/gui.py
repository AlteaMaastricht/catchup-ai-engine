"""Tkinter front-end: a visual hex board to play Catchup against the engine.

Stdlib only.  Launch with ``python3 main.py --gui`` or ``python3 -m catchup.gui``.

Click empty cells to choose the stones for your turn (1, 2 or 3, as the rules
allow), then **Next player** to commit.  The engine replies on a background
thread so the clocks and board stay live.  Stones from the last completed turn
are marked with a gold ring.
"""

import math
import queue
import threading
import tkinter as tk
from tkinter import ttk, messagebox
from time import perf_counter

from .board import (
    BLACK,
    CELL_NAMES,
    CELL_X,
    CELL_Y,
    COLOUR_NAMES,
    NEIGHBOURS,
    NUM_CELLS,
    STONE,
    WHITE,
    ZOBRIST_ALLOWS_THREE,
    ZOBRIST_CELL,
    ZOBRIST_SIDE,
)
from .record import GameSession
from .search import Clock, Engine
from .state import GameState, IllegalMove
from .ui import describe_allowance, format_result, format_turn

SQRT3 = math.sqrt(3.0)

# Centre-to-centre distance of neighbouring cells, in canvas units.
HEX_SIZE = 38.0
HEX_RADIUS = HEX_SIZE / SQRT3          # centre to vertex, pointy-top
STONE_RADIUS = HEX_RADIUS * 0.62
HIT_RADIUS = HEX_SIZE * 0.55
CANVAS_PAD = 36.0

BG = "#151922"
PANEL = "#1e2433"
CANVAS_BG = "#12161f"
HEX_EMPTY = "#2a3348"
HEX_HOVER = "#3a4864"
HEX_LINE = "#4a5870"
LABEL = "#8b95a8"
WHITE_FILL = "#f3efe4"
WHITE_LINE = "#c4bba8"
BLACK_FILL = "#1c1c1c"
BLACK_LINE = "#6a6a6a"
LAST_RING = "#f5c542"
PENDING_RING = "#5ec8f0"
HINT_RING = "#c084fc"
TEXT = "#e8ecf4"
MUTED = "#9aa3b5"
ACCENT = "#7eb6ff"
OK_GREEN = "#6bcb8b"
BAD_RED = "#e06c75"


# --- geometry (pure; tested without opening a window) -----------------------


def cell_offset(cell, size=HEX_SIZE):
    """Untranslated canvas offset of a cell centre.

    Axial ``(x, y)`` is laid out so that ``(x+1, y)`` is due east, ``(0, +1)``
    is 120 degrees (up-west) and ``(+1, +1)`` is 60 degrees (up-east).  That is
    the lattice whose six neighbours match :data:`board.NEIGHBOUR_DELTAS`.
    Higher row numbers sit higher on the screen (negative canvas y).
    """
    x = CELL_X[cell]
    y = CELL_Y[cell]
    return size * (x - 0.5 * y), -size * (SQRT3 * 0.5) * y


def board_bbox(size=HEX_SIZE, radius=HEX_RADIUS):
    xs, ys = [], []
    for cell in range(NUM_CELLS):
        px, py = cell_offset(cell, size)
        xs.append(px)
        ys.append(py)
    return (min(xs) - radius, min(ys) - radius,
            max(xs) + radius, max(ys) + radius)


def canvas_origin(width, height, size=HEX_SIZE):
    """Translation that centres the hexagon in a canvas of the given size."""
    x0, y0, x1, y1 = board_bbox(size)
    return (width - (x0 + x1)) * 0.5, (height - (y0 + y1)) * 0.5


def cell_center(cell, origin, size=HEX_SIZE):
    px, py = cell_offset(cell, size)
    return origin[0] + px, origin[1] + py


def hex_vertices(cx, cy, radius=HEX_RADIUS):
    """Flat list ``[x0, y0, ..., x5, y5]`` for a pointy-top hexagon."""
    pts = []
    for i in range(6):
        angle = math.radians(90.0 + i * 60.0)
        pts.append(cx + radius * math.cos(angle))
        pts.append(cy - radius * math.sin(angle))
    return pts


def cell_at(px, py, origin, size=HEX_SIZE, hit=HIT_RADIUS):
    """Nearest cell whose centre is within ``hit`` of ``(px, py)``, or ``None``."""
    best = None
    best_d2 = hit * hit
    for cell in range(NUM_CELLS):
        cx, cy = cell_center(cell, origin, size)
        dx = px - cx
        dy = py - cy
        d2 = dx * dx + dy * dy
        if d2 <= best_d2:
            best_d2 = d2
            best = cell
    return best


def format_clock(seconds):
    """``M:SS.d``; negative remaining time is shown as zero."""
    if seconds < 0:
        seconds = 0.0
    minutes = int(seconds) // 60
    rest = seconds - minutes * 60
    return "%d:%04.1f" % (minutes, rest)


def parse_time_limit(text, default=600.0, lo=10.0, hi=3600.0):
    """Clamp a Seconds / side field to a usable whole-game budget."""
    try:
        value = float(str(text).strip())
    except (TypeError, ValueError):
        value = default
    if value != value:  # NaN
        value = default
    return max(lo, min(value, hi))


def format_game_caption(seconds):
    """Sidebar blurb for the configured time control."""
    whole = int(round(seconds))
    minutes = seconds / 60.0
    if seconds >= 60.0 and abs(seconds - round(minutes) * 60.0) < 0.05:
        return "hexhex-5  ·  %d-minute game" % int(round(minutes))
    if abs(seconds - whole) < 0.05:
        return "hexhex-5  ·  %ds / side" % whole
    return "hexhex-5  ·  %s / side" % format_clock(seconds)


def rewind_clock_history(clocks, history, n):
    """Pop ``n`` turn records and restore each mover's clock.

    Each history entry is ``(stones, mover, used_before)``.  Returns the
    stone counts in the order they should be undone on the board.
    """
    counts = []
    for _ in range(n):
        if not history:
            break
        stones, mover, used_before = history.pop()
        clocks[mover].used = used_before
        clocks[mover].undo_note()
        clocks[1 - mover].undo_note()
        counts.append(stones)
    return counts


# --- independent sanity check -----------------------------------------------


def _flood_groups(state, player):
    stone = STONE[player]
    colour = state.colour
    seen = [False] * NUM_CELLS
    sizes = []
    for start in range(NUM_CELLS):
        if colour[start] != stone or seen[start]:
            continue
        stack = [start]
        seen[start] = True
        count = 0
        while stack:
            cell = stack.pop()
            count += 1
            for neighbour in NEIGHBOURS[cell]:
                if not seen[neighbour] and colour[neighbour] == stone:
                    seen[neighbour] = True
                    stack.append(neighbour)
        sizes.append(count)
    sizes.sort(reverse=True)
    return sizes


def _recompute_zobrist(state):
    key = 0
    for cell in range(NUM_CELLS):
        value = state.colour[cell]
        if value:
            key ^= ZOBRIST_CELL[value - 1][cell]
    if state.to_move == BLACK:
        key ^= ZOBRIST_SIDE
    if state.allows_three:
        key ^= ZOBRIST_ALLOWS_THREE
    return key


def sanity_check(state):
    """Compare incremental accumulators with a from-scratch rebuild.

    Returns ``(ok, lines)``.  ``lines`` always starts with a one-line summary.
    """
    problems = []
    occupied = 0
    for cell in range(NUM_CELLS):
        value = state.colour[cell]
        if value not in (0, 1, 2):
            problems.append("cell %s has colour %r" % (CELL_NAMES[cell], value))
        if value:
            occupied += 1
            owner = value - 1
            if not (state.bb[owner] & (1 << cell)):
                problems.append("%s missing from %s bitboard"
                                % (CELL_NAMES[cell], COLOUR_NAMES[owner]))
            other = 1 - owner
            if state.bb[other] & (1 << cell):
                problems.append("%s set on both bitboards" % CELL_NAMES[cell])

    if occupied != NUM_CELLS - state.empty_count:
        problems.append("empty_count %d but %d occupied"
                        % (state.empty_count, occupied))

    for player in (WHITE, BLACK):
        groups = _flood_groups(state, player)
        largest = max(groups) if groups else 1
        nstones = sum(groups)
        if state.group_sizes(player) != groups:
            problems.append("%s groups %s vs flood-fill %s"
                            % (COLOUR_NAMES[player], state.group_sizes(player), groups))
        if state.largest[player] != largest:
            problems.append("%s largest %d vs %d"
                            % (COLOUR_NAMES[player], state.largest[player], largest))
        if state.nstones[player] != nstones:
            problems.append("%s nstones %d vs %d"
                            % (COLOUR_NAMES[player], state.nstones[player], nstones))
        if state.score(player) != largest:
            problems.append("%s score %d vs largest %d"
                            % (COLOUR_NAMES[player], state.score(player), largest))

    expected_z = _recompute_zobrist(state)
    if state.zobrist != expected_z:
        problems.append("zobrist mismatch")

    if not 0 <= state.placed_this_turn <= state.max_this_turn <= 3:
        problems.append("turn counters %d/%d"
                        % (state.placed_this_turn, state.max_this_turn))
    if state.to_move not in (WHITE, BLACK):
        problems.append("to_move %r" % (state.to_move,))

    if problems:
        return False, ["Sanity check FAILED (%d)" % len(problems)] + problems
    return True, [
        "Sanity check OK  empty=%d  White %d  Black %d  %s to move"
        % (state.empty_count, state.score(WHITE), state.score(BLACK),
           COLOUR_NAMES[state.to_move]),
    ]


# --- application ------------------------------------------------------------


class CatchupApp(object):
    """One window: hex board, clocks, and the engine on a worker thread."""

    def __init__(self, root, total_time=600.0):
        self.root = root
        self.total_time = float(total_time)
        self.engine = Engine()
        self.state = GameState()
        self.clocks = {WHITE: Clock(self.total_time), BLACK: Clock(self.total_time)}
        self.human_side = WHITE
        self.running = False
        self.thinking = False
        self.job_id = 0
        self.turn_started = None
        self.pending = []
        self.last_cells = []
        self.hint_cells = []
        self.history = []
        self.session = None
        self._engine_budget = None
        self.hover = None
        self.origin = (0.0, 0.0)
        self._tick_after = None
        self._queue_after = None
        self._queue_alive = True
        self.task_queue = queue.Queue()

        root.title("Catchup")
        root.configure(bg=BG)
        root.minsize(960, 640)

        self._build()
        self._layout_board()
        self._redraw()
        self._refresh_sidebar()
        self._log("Click Start to begin.  You place stones, then Next player.")
        self._drain_search_queue()

    # --- widgets ------------------------------------------------------------

    def _build(self):
        style = ttk.Style()
        try:
            style.theme_use("clam")
        except tk.TclError:
            pass
        style.configure("TFrame", background=BG)
        style.configure("TLabel", background=BG, foreground=TEXT)
        style.configure("Muted.TLabel", background=BG, foreground=MUTED)
        style.configure("TRadiobutton", background=BG, foreground=TEXT)
        style.configure("TButton", padding=6)
        style.configure("Accent.TButton", padding=6)

        shell = ttk.Frame(self.root)
        shell.pack(fill=tk.BOTH, expand=True, padx=12, pady=12)

        left = ttk.Frame(shell)
        left.pack(side=tk.LEFT, fill=tk.BOTH, expand=True)

        self.canvas = tk.Canvas(
            left, bg=CANVAS_BG, highlightthickness=0,
            width=640, height=620, cursor="hand2")
        self.canvas.pack(fill=tk.BOTH, expand=True)
        self.canvas.bind("<Configure>", self._on_resize)
        self.canvas.bind("<Button-1>", self._on_click)
        self.canvas.bind("<Double-Button-1>", self._on_double_click)
        self.canvas.bind("<Button-3>", self._on_right_click)
        self.canvas.bind("<Motion>", self._on_motion)
        self.canvas.bind("<Leave>", self._on_leave)

        right = ttk.Frame(shell, width=340)
        right.pack(side=tk.RIGHT, fill=tk.Y, padx=(14, 0))
        right.pack_propagate(False)

        ttk.Label(right, text="CATCHUP", font=("Helvetica", 18, "bold")).pack(
            anchor=tk.W)
        self.caption = ttk.Label(
            right, text=format_game_caption(self.total_time),
            style="Muted.TLabel")
        self.caption.pack(anchor=tk.W, pady=(0, 10))

        clocks = ttk.Frame(right)
        clocks.pack(fill=tk.X, pady=(0, 10))
        self.white_clock = tk.Label(
            clocks, text="0:00.0", font=("Menlo", 22, "bold"),
            bg=PANEL, fg=WHITE_FILL, padx=12, pady=8)
        self.black_clock = tk.Label(
            clocks, text="0:00.0", font=("Menlo", 22, "bold"),
            bg=PANEL, fg="#d0d0d0", padx=12, pady=8)
        self.white_name = tk.Label(
            clocks, text="White", bg=BG, fg=MUTED, font=("Helvetica", 11))
        self.black_name = tk.Label(
            clocks, text="Black", bg=BG, fg=MUTED, font=("Helvetica", 11))
        self.white_name.grid(row=0, column=0, sticky=tk.W)
        self.black_name.grid(row=0, column=1, sticky=tk.W, padx=(8, 0))
        self.white_clock.grid(row=1, column=0, sticky=tk.EW, padx=(0, 8))
        self.black_clock.grid(row=1, column=1, sticky=tk.EW)
        clocks.columnconfigure(0, weight=1)
        clocks.columnconfigure(1, weight=1)

        ttk.Label(right, text="You play", style="Muted.TLabel").pack(
            anchor=tk.W, pady=(4, 0))
        self.side_var = tk.IntVar(value=WHITE)
        sides = ttk.Frame(right)
        sides.pack(anchor=tk.W, pady=(2, 8))
        self.side_buttons = []
        for text, value in (("White", WHITE), ("Black", BLACK), ("Watch", -1)):
            btn = ttk.Radiobutton(sides, text=text, variable=self.side_var,
                                  value=value, command=self._on_side)
            btn.pack(side=tk.LEFT, padx=(0, 8))
            self.side_buttons.append(btn)

        time_row = ttk.Frame(right)
        time_row.pack(fill=tk.X, pady=(0, 10))
        ttk.Label(time_row, text="Seconds / side", style="Muted.TLabel").pack(
            side=tk.LEFT)
        self.time_var = tk.StringVar(value=str(int(self.total_time)))
        self.time_entry = ttk.Entry(time_row, textvariable=self.time_var, width=7)
        self.time_entry.pack(side=tk.RIGHT)
        self.time_var.trace_add("write", self._on_time_edited)

        btns = ttk.Frame(right)
        btns.pack(fill=tk.X, pady=(0, 4))
        self.start_btn = ttk.Button(btns, text="Start", command=self.start_game)
        self.next_btn = ttk.Button(btns, text="Next player", command=self.next_player)
        self.start_btn.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 4))
        self.next_btn.pack(side=tk.LEFT, expand=True, fill=tk.X)

        btns2 = ttk.Frame(right)
        btns2.pack(fill=tk.X, pady=(0, 10))
        self.restart_btn = ttk.Button(btns2, text="Restart", command=self.restart)
        self.sanity_btn = ttk.Button(btns2, text="Sanity check",
                                     command=self.run_sanity)
        self.restart_btn.pack(side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 4))
        self.sanity_btn.pack(side=tk.LEFT, expand=True, fill=tk.X)

        extras = ttk.Frame(right)
        extras.pack(fill=tk.X, pady=(0, 10))
        ttk.Button(extras, text="Undo", command=self.undo).pack(
            side=tk.LEFT, expand=True, fill=tk.X, padx=(0, 4))
        ttk.Button(extras, text="Hint", command=self.hint).pack(
            side=tk.LEFT, expand=True, fill=tk.X)
        ttk.Button(extras, text="Clear pick", command=self.clear_pending).pack(
            side=tk.LEFT, expand=True, fill=tk.X, padx=(4, 0))

        self.status = tk.Label(
            right, text="", justify=tk.LEFT, anchor=tk.NW, wraplength=320,
            bg=PANEL, fg=TEXT, font=("Helvetica", 12), padx=10, pady=8)
        self.status.pack(fill=tk.X, pady=(0, 8))

        self.engine_label = tk.Label(
            right, text="Engine idle", justify=tk.LEFT, anchor=tk.W,
            bg=BG, fg=MUTED, font=("Menlo", 10))
        self.engine_label.pack(fill=tk.X, pady=(0, 6))

        ttk.Label(right, text="Log", style="Muted.TLabel").pack(anchor=tk.W)
        self.log = tk.Text(
            right, height=10, bg=PANEL, fg=TEXT, insertbackground=TEXT,
            relief=tk.FLAT, font=("Menlo", 10), wrap=tk.WORD, state=tk.DISABLED)
        self.log.pack(fill=tk.BOTH, expand=True, pady=(2, 0))

        self.root.bind("<Return>", lambda e: self.next_player())
        self.root.bind("<Escape>", lambda e: self.clear_pending())
        self.root.bind("u", lambda e: self.undo())
        self.root.protocol("WM_DELETE_WINDOW", self._on_close)

    def _on_close(self):
        self._flush_session(abandoned=True, state=self.state)
        self._queue_alive = False
        self.job_id += 1
        self.engine.deadline = 0.0
        if self._tick_after is not None:
            try:
                self.root.after_cancel(self._tick_after)
            except tk.TclError:
                pass
            self._tick_after = None
        if self._queue_after is not None:
            try:
                self.root.after_cancel(self._queue_after)
            except tk.TclError:
                pass
            self._queue_after = None
        self.root.destroy()

    # --- layout / drawing ---------------------------------------------------

    def _layout_board(self):
        width = max(self.canvas.winfo_width(), 640)
        height = max(self.canvas.winfo_height(), 620)
        self.origin = canvas_origin(width, height)

    def _on_resize(self, event):
        self.origin = canvas_origin(event.width, event.height)
        self._redraw()

    def _redraw(self):
        c = self.canvas
        c.delete("all")
        colour = self.state.colour
        last = set(self.last_cells)
        pending = set(self.pending)
        hint = set(self.hint_cells)

        for cell in range(NUM_CELLS):
            cx, cy = cell_center(cell, self.origin)
            fill = HEX_EMPTY
            if cell == self.hover and not colour[cell] and cell not in pending:
                fill = HEX_HOVER
            c.create_polygon(
                *hex_vertices(cx, cy), fill=fill, outline=HEX_LINE, width=1.5,
                tags=("cell", "c%d" % cell))

            value = colour[cell]
            if value == 0 and cell not in pending:
                c.create_text(
                    cx, cy, text=CELL_NAMES[cell].lower(),
                    fill=LABEL, font=("Helvetica", 9))
            else:
                if cell in pending and not value:
                    stone_fill, stone_line = (
                        (WHITE_FILL, WHITE_LINE) if self.state.to_move == WHITE
                        else (BLACK_FILL, BLACK_LINE))
                    c.create_oval(
                        cx - STONE_RADIUS, cy - STONE_RADIUS,
                        cx + STONE_RADIUS, cy + STONE_RADIUS,
                        fill=stone_fill, outline=stone_line, width=1)
                elif value:
                    if value == 1:
                        stone_fill, stone_line = WHITE_FILL, WHITE_LINE
                    else:
                        stone_fill, stone_line = BLACK_FILL, BLACK_LINE
                    c.create_oval(
                        cx - STONE_RADIUS, cy - STONE_RADIUS,
                        cx + STONE_RADIUS, cy + STONE_RADIUS,
                        fill=stone_fill, outline=stone_line, width=1.5)

            ring_r = STONE_RADIUS + 5
            if cell in last:
                c.create_oval(
                    cx - ring_r, cy - ring_r, cx + ring_r, cy + ring_r,
                    outline=LAST_RING, width=3)
            elif cell in pending:
                c.create_oval(
                    cx - ring_r, cy - ring_r, cx + ring_r, cy + ring_r,
                    outline=PENDING_RING, width=3)
            elif cell in hint:
                c.create_oval(
                    cx - ring_r, cy - ring_r, cx + ring_r, cy + ring_r,
                    outline=HINT_RING, width=2, dash=(5, 3))

        if self.thinking:
            width = c.winfo_width()
            c.create_text(
                width * 0.5, 18, text="engine thinking…",
                fill=ACCENT, font=("Helvetica", 12, "italic"))

    def _refresh_sidebar(self):
        state = self.state
        self._paint_clocks()
        if state.is_over:
            body = "Game over\n" + format_result(state).replace("  ", "")
        else:
            body = (
                "White score %d    Black score %d\n"
                "%d empty    turn %d\n\n"
                "%s to move\n"
                "May place %s\n\n"
                "Picked: %s"
                % (
                    state.score(WHITE), state.score(BLACK),
                    state.empty_count, state.turn_index + 1,
                    COLOUR_NAMES[state.to_move],
                    describe_allowance(state),
                    format_turn(self.pending) if self.pending else "(none)",
                )
            )
        self.status.configure(text=body)
        can_advance = (
            self.running and not self.thinking and not state.is_over
            and (self._is_human_turn() or self.human_side is None)
        )
        self.next_btn.configure(state=(tk.NORMAL if can_advance else tk.DISABLED))
        self.start_btn.configure(state=(tk.DISABLED if self.running else tk.NORMAL))
        lock = tk.DISABLED if self.running else tk.NORMAL
        self.time_entry.configure(state=lock)
        for btn in self.side_buttons:
            btn.configure(state=lock)

    def _paint_clocks(self):
        now = perf_counter()
        for player, label in ((WHITE, self.white_clock), (BLACK, self.black_clock)):
            live = 0.0
            if (self.running and not self.state.is_over
                    and self.turn_started is not None
                    and self.state.to_move == player):
                live = now - self.turn_started
            remaining = self.clocks[player].total - self.clocks[player].used - live
            label.configure(text=format_clock(remaining))
            active = (self.running and self.state.to_move == player
                      and not self.state.is_over)
            label.configure(
                highlightthickness=2 if active else 0,
                highlightbackground=LAST_RING if active else PANEL,
                highlightcolor=LAST_RING)

    def _log(self, line):
        self.log.configure(state=tk.NORMAL)
        self.log.insert(tk.END, line + "\n")
        self.log.see(tk.END)
        self.log.configure(state=tk.DISABLED)
        if self.session is not None:
            self.session.note(line)

    def _flush_session(self, abandoned=False, state=None):
        session = self.session
        self.session = None
        self._engine_budget = None
        if session is None:
            return
        path = session.finish(state=state, abandoned=abandoned)
        if path is not None:
            try:
                self._log("Saved %s" % path)
            except tk.TclError:
                pass

    # --- clocks -------------------------------------------------------------

    def _begin_turn_clock(self):
        self.turn_started = perf_counter()
        if self._tick_after is None:
            self._tick()

    def _end_turn_clock(self, player):
        if self.turn_started is None:
            return
        self.clocks[player].consume(perf_counter() - self.turn_started)
        self.turn_started = None

    def _tick(self):
        self._paint_clocks()
        if self.running and not self.state.is_over:
            self._tick_after = self.root.after(200, self._tick)
        else:
            self._tick_after = None

    # --- game control -------------------------------------------------------

    def _read_time(self):
        return parse_time_limit(self.time_var.get())

    def _reset_clocks(self, total):
        """Rebuild both clocks so timers and engine allocate() share ``total``."""
        self.total_time = float(total)
        self.clocks = {WHITE: Clock(self.total_time), BLACK: Clock(self.total_time)}
        self.caption.configure(text=format_game_caption(self.total_time))

    def _on_time_edited(self, *_args):
        if self.running:
            return
        raw = self.time_var.get().strip()
        if not raw:
            return
        try:
            value = float(raw)
        except ValueError:
            return
        if value < 10.0 or value > 3600.0:
            return
        self._reset_clocks(value)
        self._paint_clocks()

    def _on_side(self):
        if self.running:
            return
        value = self.side_var.get()
        self.human_side = None if value < 0 else value

    def _is_human_turn(self):
        if self.human_side is None:
            return False
        return self.state.to_move == self.human_side

    def start_game(self):
        if self.running:
            return
        self._flush_session(abandoned=True, state=self.state)
        self._reset_clocks(self._read_time())
        self.time_var.set(str(int(self.total_time)))
        self.human_side = None if self.side_var.get() < 0 else self.side_var.get()
        self.running = True
        self.session = GameSession(
            self.total_time, self.human_side, source="gui", engine=self.engine)
        self._begin_turn_clock()
        self._refresh_sidebar()
        who = "watch both" if self.human_side is None else COLOUR_NAMES[self.human_side]
        self._log("Started.  You play %s.  %s / side."
                  % (who, format_clock(self.total_time)))
        if not self._is_human_turn():
            self._kick_engine()

    def restart(self):
        self._flush_session(abandoned=True, state=self.state)
        self.job_id += 1
        self.engine.deadline = 0.0
        self.engine.new_game()
        self.state = GameState()
        self._reset_clocks(self._read_time())
        self.time_var.set(str(int(self.total_time)))
        self.running = False
        self.thinking = False
        self.turn_started = None
        self.pending = []
        self.last_cells = []
        self.hint_cells = []
        self.history = []
        self.engine_label.configure(text="Engine idle", fg=MUTED)
        self._log("Board reset.")
        self._redraw()
        self._refresh_sidebar()

    def next_player(self):
        if not self.running or self.thinking or self.state.is_over:
            return
        if not self._is_human_turn():
            if self.human_side is None:
                self._kick_engine()
            return
        if not self.pending:
            self._log("Pick 1 to %d empty cell(s), then Next player."
                      % self.state.max_this_turn)
            return
        self._commit_human(list(self.pending))

    def clear_pending(self):
        if self.pending:
            self.pending = []
            self._redraw()
            self._refresh_sidebar()

    def undo(self):
        if self.thinking or not self.history:
            return
        if self.running and self.turn_started is not None:
            # Do not charge the aborted think / pick for the undone turn.
            self.turn_started = perf_counter()
        n = 2 if self.human_side is not None and len(self.history) >= 2 else 1
        for stones in rewind_clock_history(self.clocks, self.history, n):
            self.state.undo_turn(stones)
        if self.session is not None:
            self.session.undo(n)
        self.pending = []
        self.last_cells = []
        self.hint_cells = []
        self._log("Undo.")
        self._redraw()
        self._refresh_sidebar()
        if (self.running and self.human_side is not None
                and not self._is_human_turn() and not self.state.is_over):
            self._kick_engine()

    def run_sanity(self):
        ok, lines = sanity_check(self.state)
        for line in lines:
            self._log(line)
        self.engine_label.configure(
            text=lines[0], fg=(OK_GREEN if ok else BAD_RED))
        if not ok:
            messagebox.showerror("Sanity check", "\n".join(lines[:12]))
        else:
            messagebox.showinfo("Sanity check", lines[0])

    def hint(self):
        if not self.running or self.thinking or self.state.is_over:
            return
        if not self._is_human_turn():
            self._log("Hint is for your turn.")
            return
        budget = min(4.0, max(0.2, self.clocks[self.state.to_move].allocate(self.state)))
        self._engine_budget = budget
        self._log("Hint: searching %.1fs…" % budget)
        self._search_async(self.state.clone(), budget, self._on_hint)

    # --- mouse --------------------------------------------------------------

    def _event_cell(self, event):
        return cell_at(event.x, event.y, self.origin)

    def _on_motion(self, event):
        cell = self._event_cell(event)
        if cell != self.hover:
            self.hover = cell
            self._redraw()

    def _on_leave(self, _event):
        if self.hover is not None:
            self.hover = None
            self._redraw()

    def _on_click(self, event):
        if not self.running or self.thinking or self.state.is_over:
            return
        if not self._is_human_turn():
            return
        cell = self._event_cell(event)
        if cell is None:
            return
        if cell in self.pending:
            self.pending.remove(cell)
        elif self.state.colour[cell]:
            self._log("%s is occupied." % CELL_NAMES[cell])
            return
        elif len(self.pending) >= self.state.max_this_turn:
            self._log("You may place at most %d this turn.  Next player to commit."
                      % self.state.max_this_turn)
            return
        else:
            self.pending.append(cell)
            self.hint_cells = []
        self._redraw()
        self._refresh_sidebar()

    def _on_double_click(self, _event):
        # Button-1 already selected the cell; a second _on_click would toggle it off.
        if self.pending:
            self.next_player()

    def _on_right_click(self, event):
        if self.pending:
            self.pending.pop()
            self._redraw()
            self._refresh_sidebar()
            return
        self._on_click(event)

    # --- making a turn ------------------------------------------------------

    def _commit_human(self, cells):
        mover = self.state.to_move
        empty_before = self.state.empty_count
        try:
            self.state.play_turn(cells)
        except IllegalMove as exc:
            self._log("Illegal: %s" % exc)
            return
        stones = empty_before - self.state.empty_count
        if stones < 0:
            stones = 0
        self.clocks[mover].note_own_stones(stones)
        self.clocks[1 - mover].note_reply_stones(stones)
        used_before = self.clocks[mover].used
        remaining_before = self.clocks[mover].total - used_before
        self._end_turn_clock(mover)
        think_s = self.clocks[mover].used - used_before
        remaining_after = self.clocks[mover].total - self.clocks[mover].used
        self.history.append((len(cells), mover, used_before))
        if self.session is not None:
            self.session.add_turn(
                cells, mover, "human", think_s, remaining_before, remaining_after)
        self.last_cells = list(cells)
        self.pending = []
        self.hint_cells = []
        self._log("%s plays %s" % (COLOUR_NAMES[mover], format_turn(cells)))
        self._after_turn()

    def _after_turn(self):
        self._redraw()
        self._refresh_sidebar()
        if self.state.is_over:
            self.running = True
            self._log(format_result(self.state).strip())
            self._flush_session(abandoned=False, state=self.state)
            messagebox.showinfo("Game over", format_result(self.state))
            return
        self._begin_turn_clock()
        self._refresh_sidebar()
        if not self._is_human_turn():
            self._kick_engine()

    def _kick_engine(self):
        if self.thinking or self.state.is_over:
            return
        budget = self.clocks[self.state.to_move].allocate(self.state)
        self._engine_budget = budget
        self.engine_label.configure(
            text="Engine thinking  (%.1fs)…" % budget, fg=ACCENT)
        self._log("%s (engine) thinking %.1fs…"
                  % (COLOUR_NAMES[self.state.to_move], budget))
        self._search_async(self.state.clone(), budget, self._on_engine_move)

    def _drain_search_queue(self):
        """Apply engine results on the Tk thread.

        PyPy's Tk forbids ``after`` / ``createcommand`` from a worker thread.
        The search thread only ``Queue.put``s; this poller runs on the UI loop.
        """
        if not self._queue_alive:
            return
        while True:
            try:
                job, callback, turn, stats, error = self.task_queue.get_nowait()
            except queue.Empty:
                break
            callback(job, turn, stats, error)
        try:
            self._queue_after = self.root.after(40, self._drain_search_queue)
        except tk.TclError:
            self._queue_alive = False

    def _search_async(self, snapshot, budget, callback):
        self.thinking = True
        self.job_id += 1
        job = self.job_id
        self._redraw()
        self._refresh_sidebar()

        def worker():
            try:
                turn, stats = self.engine.find_turn(snapshot, budget)
                error = None
            except Exception as exc:
                turn, stats, error = None, None, exc
            self.task_queue.put((job, callback, turn, stats, error))

        threading.Thread(target=worker, daemon=True).start()

    def _on_engine_move(self, job, turn, stats, error):
        if job != self.job_id:
            return
        self.thinking = False
        if error is not None:
            self.engine_label.configure(text="Engine error", fg=BAD_RED)
            self._log("Engine error: %s" % error)
            self._redraw()
            self._refresh_sidebar()
            return
        mover = self.state.to_move
        empty_before = self.state.empty_count
        try:
            self.state.play_turn(turn)
        except IllegalMove as exc:
            self._log("Engine illegal: %s  (%s)" % (exc, turn))
            self._redraw()
            self._refresh_sidebar()
            return
        stones = empty_before - self.state.empty_count
        if stones < 0:
            stones = 0
        self.clocks[mover].note_own_stones(stones)
        self.clocks[1 - mover].note_reply_stones(stones)
        used_before = self.clocks[mover].used
        remaining_before = self.clocks[mover].total - used_before
        self._end_turn_clock(mover)
        think_s = self.clocks[mover].used - used_before
        remaining_after = self.clocks[mover].total - self.clocks[mover].used
        self.history.append((len(turn), mover, used_before))
        if self.session is not None:
            self.session.add_turn(
                turn, mover, "engine", think_s, remaining_before, remaining_after,
                budget_s=self._engine_budget, stats=stats)
        self.last_cells = list(turn)
        self.engine_label.configure(text=stats.describe(), fg=MUTED)
        self._log("%s plays %s" % (COLOUR_NAMES[mover], format_turn(turn)))
        self._log("  %s | %.0fs left"
                  % (stats.describe(), self.clocks[mover].total - self.clocks[mover].used))
        self._after_turn()

    def _on_hint(self, job, turn, stats, error):
        if job != self.job_id:
            return
        self.thinking = False
        if error is not None:
            self._log("Hint failed: %s" % error)
        else:
            self.hint_cells = list(turn)
            if self.session is not None:
                self.session.add_hint(turn, stats, self._engine_budget or 0.0)
            self._log("Hint: %s   (%s)" % (format_turn(turn), stats.describe()))
            self.engine_label.configure(text="Hint  " + stats.describe(), fg=HINT_RING)
        self._redraw()
        self._refresh_sidebar()


def launch(total_time=600.0):
    """Open the Catchup window and run the Tk event loop."""
    root = tk.Tk()
    CatchupApp(root, total_time=total_time)
    root.mainloop()


def main(argv=None):
    import argparse
    parser = argparse.ArgumentParser(description="Catchup graphical board")
    parser.add_argument("--time", type=float, default=600.0,
                        help="seconds per side (default 600)")
    args = parser.parse_args(argv)
    launch(total_time=args.time)
    return 0


if __name__ == "__main__":
    import sys
    sys.exit(main())
