"""Persist human-vs-engine (and watch-both) games for later tuning.

Games are appended as JSON lines under::

    games/<seconds>s/human_vs_ai.jsonl
    games/<seconds>s/engine_vs_engine.jsonl

``<seconds>`` is the Seconds / side budget (e.g. ``30s``, ``300s``, ``600s``).
"""

from __future__ import annotations

import json
import uuid
from datetime import datetime, timezone
from pathlib import Path

from .board import BLACK, CELL_NAMES, WHITE
from .search import TT_BITS

GAMES_ROOT = Path(__file__).resolve().parent.parent / "games"


def seconds_bucket(total_time):
    return max(1, int(round(float(total_time))))


def mode_name(human_side):
    if human_side is None:
        return "engine_vs_engine"
    if human_side == WHITE:
        return "human_white"
    return "human_black"


def log_filename(human_side):
    return "engine_vs_engine.jsonl" if human_side is None else "human_vs_ai.jsonl"


def game_dir(total_time, root=None):
    base = Path(root) if root is not None else GAMES_ROOT
    return base / ("%ds" % seconds_bucket(total_time))


def engine_snapshot(engine=None):
    info = {"tt_bits": TT_BITS}
    settings = getattr(engine, "settings", None)
    if isinstance(settings, dict):
        info.update(settings)
    return info


def stats_dict(stats):
    if stats is None:
        return None
    elapsed = float(stats.elapsed)
    return {
        "depth": int(stats.depth),
        "completed": int(stats.completed),
        "value": int(stats.value),
        "nodes": int(stats.nodes),
        "elapsed": elapsed,
        "nps": (stats.nodes / elapsed) if elapsed > 0 else 0.0,
        "turn": [CELL_NAMES[c] for c in stats.turn],
    }


def result_dict(state):
    from .ui import format_result

    white = list(state.group_sizes(WHITE))
    black = list(state.group_sizes(BLACK))
    outcome = int(state.compare_final())
    if outcome > 0:
        winner = "White"
    elif outcome < 0:
        winner = "Black"
    else:
        winner = "draw"
    return {
        "winner": winner,
        "compare": outcome,
        "white_groups": white,
        "black_groups": black,
        "white_score": int(state.score(WHITE)),
        "black_score": int(state.score(BLACK)),
        "text": format_result(state).strip(),
    }


class GameSession:
    """One GUI or console game, written once it ends or is abandoned."""

    def __init__(self, total_time, human_side, source="gui", engine=None,
                 root=None):
        now = datetime.now(timezone.utc)
        self.total_time = float(total_time)
        self.human_side = human_side
        self.root = Path(root) if root is not None else GAMES_ROOT
        self.record = {
            "schema": 1,
            "id": now.strftime("%Y%m%dT%H%M%S") + "-" + uuid.uuid4().hex[:8],
            "started_at": now.isoformat(),
            "finished_at": None,
            "seconds_per_side": self.total_time,
            "mode": mode_name(human_side),
            "human_side": None if human_side is None else int(human_side),
            "source": source,
            "engine": engine_snapshot(engine),
            "abandoned": False,
            "undo_count": 0,
            "hints": [],
            "turns": [],
            "log": [],
            "result": None,
        }

    def note(self, line):
        self.record["log"].append(line)

    def add_hint(self, cells, stats, budget):
        self.record["hints"].append({
            "ply": len(self.record["turns"]),
            "cells": [CELL_NAMES[c] for c in cells],
            "budget_s": float(budget),
            "engine": stats_dict(stats),
        })

    def add_turn(self, cells, mover, actor, think_s, clock_before, clock_after,
                 budget_s=None, stats=None):
        self.record["turns"].append({
            "ply": len(self.record["turns"]),
            "mover": "White" if mover == WHITE else "Black",
            "actor": actor,
            "cells": [CELL_NAMES[c] for c in cells],
            "think_s": float(think_s),
            "clock_before": float(clock_before),
            "clock_after": float(clock_after),
            "budget_s": None if budget_s is None else float(budget_s),
            "engine": stats_dict(stats),
        })

    def undo(self, n):
        n = min(int(n), len(self.record["turns"]))
        if n <= 0:
            return
        del self.record["turns"][-n:]
        self.record["undo_count"] += 1
        for i, turn in enumerate(self.record["turns"]):
            turn["ply"] = i

    def finish(self, state=None, abandoned=False):
        self.record["finished_at"] = datetime.now(timezone.utc).isoformat()
        self.record["abandoned"] = bool(abandoned)
        if state is not None and state.is_over:
            self.record["result"] = result_dict(state)
        return self.save()

    def save(self):
        if not self.record["turns"]:
            return None
        folder = game_dir(self.total_time, self.root)
        folder.mkdir(parents=True, exist_ok=True)
        path = folder / log_filename(self.human_side)
        with path.open("a", encoding="utf-8") as fh:
            fh.write(json.dumps(self.record, ensure_ascii=True) + "\n")
        return path
