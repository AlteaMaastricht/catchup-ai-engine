#!/usr/bin/env python3
"""Texel-style logistic fit of the 15 eval weights from engine self-play.

Do not label positions with random playouts. This script:

  1. Plays the current engine against itself from the frozen opening seeds
  2. Snapshots start-of-turn positions and the game outcome
  3. Minimizes logistic loss of eval(w) vs that outcome (White's view)
  4. Prints a weight vector you can feed to catchup-match for validation

    python3 tools/tune.py --quick
    python3 tools/tune.py --games 40 --time 0.25
    python3 tools/tune.py --validate   # runs catchup-match vs the printed vector
"""

from __future__ import annotations

import argparse
import math
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT))

from catchup.board import WHITE
from catchup.search import Engine
from catchup.state import GameState
from tests.helpers import random_position

WEIGHT_NAMES = (
    "w_largest", "w_sumsq", "w_frontier", "w_centre", "w_tempo",
    "w_pending", "w_liberty", "w_stones", "w_second", "w_join",
    "w_crit", "w_third", "w_fourth", "w_gift", "w_reachable",
)
DEFAULT = [100, 2, 1, 2, 0, 25, 8, 20, 40, 4, 16, 25, 8, 80, 10]

OPENINGS = (
    (1, 0), (2, 0), (3, 0), (4, 0),
    (101, 8), (102, 8), (103, 8), (104, 8),
    (201, 16), (202, 16), (203, 16), (204, 16),
)


def _engine(weights, tt_bits=16):
    kwargs = dict(zip(WEIGHT_NAMES, weights))
    return Engine(use_opening_book=False, tt_bits=tt_bits, **kwargs)


def _start(seed, stones):
    return random_position(seed=seed, stones=stones) if stones else GameState()


def collect(games, time_each, weights, tt_bits):
    """Return list of (GameState clone, stm_is_white, y_from_white)."""
    rows = []
    engine = _engine(weights, tt_bits=tt_bits)
    for g in range(games):
        seed, stones = OPENINGS[g % len(OPENINGS)]
        state = _start(seed, stones)
        engine.new_game()
        snapshots = []
        moves = 0
        while not state.is_over and moves < 80:
            if state.placed_this_turn == 0:
                snapshots.append((state.clone(), state.to_move == WHITE))
            turn, _stats = engine.find_turn(state, time_each)
            state.play_turn(turn)
            moves += 1
        result = state.compare_final()
        if result > 0:
            y = 1.0
        elif result < 0:
            y = 0.0
        else:
            y = 0.5
        for snap, stm_white in snapshots:
            rows.append((snap, stm_white, y))
        print("  collected game %d/%d  (%d positions, result %+d)"
              % (g + 1, games, len(snapshots), result), flush=True)
    return rows


def sigmoid(x):
    if x >= 30:
        return 1.0
    if x <= -30:
        return 0.0
    return 1.0 / (1.0 + math.exp(-x))


def loss_of(engine, rows, weights, k):
    engine.set_eval_weights(weights)
    total = 0.0
    for snap, stm_white, y in rows:
        value = engine.evaluate(snap, snap.placements_left())
        if not stm_white:
            value = -value
        p = sigmoid(value / k)
        p = min(1.0 - 1e-12, max(1e-12, p))
        total += -(y * math.log(p) + (1.0 - y) * math.log(1.0 - p))
    return total / max(1, len(rows))


def fit_k(engine, rows, weights):
    best_k, best = 200.0, None
    for k in (80, 120, 160, 200, 280, 400, 600):
        L = loss_of(engine, rows, weights, k)
        if best is None or L < best:
            best, best_k = L, float(k)
    return best_k


def coordinate_descent(engine, rows, weights, k, rounds=3):
    """Tune relative weights. w_largest is frozen as the eval scale."""
    lo, hi = -200, 400
    current = list(weights)
    best_loss = loss_of(engine, rows, current, k)
    print("  start loss %.4f  K=%.0f" % (best_loss, k), flush=True)
    steps = (20, 10, 5, 2, 1)
    for rnd in range(rounds):
        for i, name in enumerate(WEIGHT_NAMES):
            if i == 0:
                continue
            for step in steps:
                best_val = current[i]
                changed = False
                for trial_val in (current[i] - step, current[i] + step):
                    if trial_val < lo or trial_val > hi:
                        continue
                    trial = list(current)
                    trial[i] = trial_val
                    L = loss_of(engine, rows, trial, k)
                    if L + 1e-4 < best_loss:
                        best_loss = L
                        best_val = trial_val
                        changed = True
                current[i] = best_val
                if changed:
                    print("    %s -> %d  loss %.4f" % (name, best_val, best_loss),
                          flush=True)
        print("  round %d loss %.4f" % (rnd + 1, best_loss), flush=True)
    return current, best_loss


def format_weights(weights):
    return ",".join(str(int(w)) for w in weights)


def validate(weights, time_each, games):
    match = ROOT / "build" / "catchup-match"
    if not match.exists():
        print("catchup-match not found at %s — build it to validate" % match)
        return 1
    cmd = [
        str(match),
        "--games", str(games),
        "--time", str(time_each),
        "--tt-bits", "18",
        "--a-weights", format_weights(DEFAULT),
        "--b-weights", format_weights(weights),
    ]
    print("running:", " ".join(cmd), flush=True)
    return subprocess.call(cmd)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--games", type=int, default=24)
    parser.add_argument("--time", type=float, default=0.25)
    parser.add_argument("--tt-bits", type=int, default=16)
    parser.add_argument("--rounds", type=int, default=2)
    parser.add_argument("--quick", action="store_true")
    parser.add_argument("--validate", action="store_true",
                        help="only run catchup-match of DEFAULT vs --weights")
    parser.add_argument("--weights", default=None,
                        help="comma-separated 15 ints (validate or start vector)")
    args = parser.parse_args(argv)

    start = list(DEFAULT)
    if args.weights:
        start = [int(x) for x in args.weights.split(",")]
        if len(start) != 15:
            parser.error("--weights needs 15 comma-separated ints")

    if args.validate:
        return validate(start, args.time, args.games)

    games = 8 if args.quick else args.games
    time_each = 0.12 if args.quick else args.time
    rounds = 1 if args.quick else args.rounds

    print("collecting %d self-play games at %.2fs/turn" % (games, time_each))
    rows = collect(games, time_each, start, args.tt_bits)
    print("dataset: %d positions" % len(rows))
    ys = [y for _s, _w, y in rows]
    mean_y = sum(ys) / len(ys)
    if mean_y < 0.15 or mean_y > 0.85:
        print("warning: lopsided outcomes (mean y=%.2f); fit may be noisy" % mean_y)
    if not rows:
        print("no positions collected")
        return 1

    tuner = _engine(start, tt_bits=12)
    k = fit_k(tuner, rows, start)
    fitted, loss = coordinate_descent(tuner, rows, start, k, rounds=rounds)
    print("\nfitted weights:")
    for name, value in zip(WEIGHT_NAMES, fitted):
        print("  %-12s %d" % (name, value))
    print("csv: %s" % format_weights(fitted))
    print("loss: %.4f  K: %.0f" % (loss, k))
    print("\nvalidate with:")
    print("  %s/build/catchup-match --games 48 --time 0.5 --a-weights %s --b-weights %s"
          % (ROOT, format_weights(DEFAULT), format_weights(fitted)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
