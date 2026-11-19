"""Console front-end: board rendering, move entry and the game loop.

The board is drawn as the hexagon of Figure 2.  Row ``y`` is indented by
``abs(4 - y)`` half-cells, and with a two-character cell pitch a half-cell is
exactly one space, so the drawing keeps the real geometry: the cell drawn up and
to the left of another really is its neighbour.

Empty cells show their column letter in lower case, so any cell can be named by
reading its letter off the board and its number off the row label.
"""

from time import perf_counter

from .board import (
    BLACK,
    CELL_NAMES,
    COLOUR_NAMES,
    DISPLAY_ROWS,
    LETTERS,
    NAME_TO_INDEX,
    NUM_CELLS,
    WHITE,
    CELL_X,
)
from .record import GameSession
from .search import Clock, Engine
from .state import GameState, IllegalMove

WHITE_GLYPH = "O"
BLACK_GLYPH = "X"

HELP_TEXT = """\
Commands
  <cells>   place stones, e.g.  e5   or   e5 d4   or   e5 d4 c3
  hint      let the engine suggest a turn for you
  board     redraw the board
  undo      take back your last turn and the engine's reply
  help      show this text
  quit      leave the game
"""


def render(state):
    """Return the board as a printable string."""
    colour = state.colour
    lines = []
    for number, indent, cells in DISPLAY_ROWS:
        glyphs = []
        for cell in cells:
            value = colour[cell]
            if value == 0:
                glyphs.append(LETTERS[CELL_X[cell]].lower())
            elif value == 1:
                glyphs.append(WHITE_GLYPH)
            else:
                glyphs.append(BLACK_GLYPH)
        lines.append("  %d  %s%s" % (number, " " * indent, " ".join(glyphs)))
    return "\n".join(lines)


def describe_allowance(state):
    """Explain, in words, how many stones the mover may place and why."""
    mover = state.to_move
    opponent = 1 - mover
    allowed = state.max_this_turn

    if state.turn_index == 0:
        return "1 stone (White's opening move is always a single stone)"

    if state.allows_three:
        reason = "%s's score rose to %d and is at least your %d" % (
            COLOUR_NAMES[opponent], state.score(opponent), state.score(mover))
    elif not state.score_increased[opponent]:
        reason = "%s's score did not rise last turn" % COLOUR_NAMES[opponent]
    else:
        reason = "%s's score of %d is below your %d" % (
            COLOUR_NAMES[opponent], state.score(opponent), state.score(mover))

    text = "1 to %d stone%s (%s)" % (allowed, "" if allowed == 1 else "s", reason)
    if allowed < (3 if state.allows_three else 2):
        text += ", capped by the %d empty cell(s) left" % state.empty_count
    return text


def format_status(state):
    lines = [
        "  White %s : score %d      Black %s : score %d      %d empty" % (
            WHITE_GLYPH, state.score(WHITE), BLACK_GLYPH, state.score(BLACK),
            state.empty_count),
    ]
    if not state.is_over:
        lines.append("  %s to move, may place %s" % (
            COLOUR_NAMES[state.to_move], describe_allowance(state)))
    return "\n".join(lines)


def format_turn(cells):
    return " ".join(CELL_NAMES[c] for c in cells)


def format_result(state):
    """Describe the finished game, including how any tie was broken."""
    white = state.group_sizes(WHITE)
    black = state.group_sizes(BLACK)
    outcome = state.compare_final()
    lines = [
        "  White groups: %s" % ", ".join(map(str, white)),
        "  Black groups: %s" % ", ".join(map(str, black)),
    ]
    if outcome == 0:
        lines.append("  Draw: every group size matches.")
    else:
        winner = WHITE if outcome == 1 else BLACK
        for i in range(max(len(white), len(black))):
            w = white[i] if i < len(white) else 0
            b = black[i] if i < len(black) else 0
            if w != b:
                ordinal = ("largest", "second largest", "third largest")
                where = ordinal[i] if i < 3 else "group number %d" % (i + 1)
                lines.append("  %s wins: %s group %d beats %d." % (
                    COLOUR_NAMES[winner], where, max(w, b), min(w, b)))
                break
    return "\n".join(lines)


def parse_turn(text, state):
    """Turn typed input into a list of cell indices.

    Raises ``ValueError`` with a message meant for the player.
    """
    tokens = text.replace(",", " ").split()
    if not tokens:
        raise ValueError("type at least one cell, for example E5")

    cells = []
    for token in tokens:
        key = token.upper()
        if key not in NAME_TO_INDEX:
            raise ValueError("%r is not a cell on this board" % token)
        cell = NAME_TO_INDEX[key]
        if state.colour[cell]:
            raise ValueError("%s is already occupied" % CELL_NAMES[cell])
        if cell in cells:
            raise ValueError("%s is listed twice" % CELL_NAMES[cell])
        cells.append(cell)

    if len(cells) > state.max_this_turn:
        raise ValueError(
            "you may place at most %d stone(s) this turn, not %d"
            % (state.max_this_turn, len(cells)))
    return cells


def _prompt_human(state, engine, clock):
    """Read one legal turn from the player, handling the commands."""
    while True:
        try:
            text = input("  %s > " % COLOUR_NAMES[state.to_move]).strip()
        except EOFError:
            return None
        if not text:
            continue

        command = text.lower()
        if command in ("quit", "exit", "q"):
            return None
        if command in ("help", "?"):
            print(HELP_TEXT)
            continue
        if command == "board":
            print(render(state))
            print(format_status(state))
            continue
        if command == "undo":
            return "undo"
        if command == "hint":
            budget = min(5.0, clock.allocate(state))
            suggestion, stats = engine.find_turn(state.clone(), budget)
            print("  hint: %s   (%s)" % (format_turn(suggestion), stats.describe()))
            continue

        try:
            return parse_turn(text, state)
        except ValueError as exc:
            print("  %s.  Type 'help' for the commands." % exc)


def play(human_side=WHITE, engine=None, total_time=600.0, verbose=True):
    """Run one interactive game.

    ``human_side`` may be :data:`board.WHITE`, :data:`board.BLACK` or ``None``
    for an engine-versus-engine demonstration.
    """
    state = GameState()
    engine = engine or Engine()
    engine.new_game()
    clocks = {WHITE: Clock(total_time), BLACK: Clock(total_time)}
    history = []                      # (stones, mover, used_before) per turn
    session = GameSession(total_time, human_side, source="console", engine=engine)

    print(HELP_TEXT)
    while not state.is_over:
        print()
        print(render(state))
        print(format_status(state))

        mover = state.to_move
        used_before = clocks[mover].used
        remaining_before = clocks[mover].total - used_before
        stats = None
        budget = None
        actor = "engine"
        if human_side is not None and mover == human_side:
            actor = "human"
            started = perf_counter()
            choice = _prompt_human(state, engine, clocks[mover])
            think_s = perf_counter() - started
            if choice is None:
                print("\n  Game abandoned.")
                path = session.finish(abandoned=True)
                if path is not None:
                    print("  Saved %s" % path)
                return None
            if choice == "undo":
                if len(history) < 2:
                    print("  Nothing to undo yet.")
                    continue
                for _ in range(2):
                    stones, mover, used_before = history.pop()
                    clocks[mover].used = used_before
                    state.undo_turn(stones)
                    clocks[mover].undo_note()
                    clocks[1 - mover].undo_note()
                session.undo(2)
                continue
            cells = choice
        else:
            budget = clocks[mover].allocate(state)
            started = perf_counter()
            cells, stats = engine.find_turn(state, budget)
            think_s = perf_counter() - started
            clocks[mover].consume(think_s)
            if verbose:
                print("  %s plays %s" % (COLOUR_NAMES[mover], format_turn(cells)))
                print("  %s | %.0fs of budget left"
                      % (stats.describe(), clocks[mover].remaining()))

        empty_before = state.empty_count
        try:
            state.play_turn(cells)
        except IllegalMove as exc:
            print("  Illegal: %s" % exc)
            continue
        stones = empty_before - state.empty_count
        if stones < 0:
            stones = 0
        clocks[mover].note_own_stones(stones)
        clocks[1 - mover].note_reply_stones(stones)
        remaining_after = clocks[mover].total - clocks[mover].used
        session.add_turn(cells, mover, actor, think_s, remaining_before,
                         remaining_after, budget_s=budget, stats=stats)
        history.append((len(cells), mover, used_before))

    print()
    print(render(state))
    print(format_status(state))
    print(format_result(state))
    path = session.finish(state)
    if path is not None:
        print("  Saved %s" % path)
    return state.compare_final()
