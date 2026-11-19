#!/usr/bin/env python3
"""Nodes/sec, depth-by-phase, and A/B self-play for the Catchup engine.

    python3 bench.py            full suite (a few minutes)
    python3 bench.py --quick    shorter run for a sanity check
    pypy3.11 bench.py --quick   same under Homebrew PyPy (binary is not pypy3)

The output compares the search techniques against each other (speed, depth,
short self-play matches).  Only the standard library is used.
"""

from __future__ import annotations

import argparse
import time

from catchup.board import BLACK, CELL_NAMES, NUM_CELLS, WHITE
from catchup.search import Engine
from catchup.state import GameState
from tests.helpers import random_position

PHASES = (
    ("opening", 0),
    ("early", 12),
    ("midgame", 28),
    ("late", 46),
)


def _engine(**overrides):
    kwargs = dict(
        use_tt=True,
        use_pvs=True,
        use_killers=True,
        use_history=True,
        use_aspiration=True,
        use_lmr=True,
        use_rfp=True,
        use_lmp=True,
        use_iid=False,
        use_opening_book=False,
        max_depth=64,
    )
    kwargs.update(overrides)
    return Engine(**kwargs)


def _play_turn(state, engine, budget):
    turn, stats = engine.find_turn(state, budget)
    state.play_turn(turn)
    return stats


def bench_nps(seconds=1.5):
    """Search a handful of positions and report nodes / second."""
    print("== nodes / second ==")
    # One engine, like a tournament game: the tracing JIT stays hot and the
    # transposition table is not rebuilt between positions.
    engine = _engine()
    engine.find_turn(random_position(seed=1, stones=20), min(1.5, max(0.4, seconds)))
    rows = []
    for name, stones in PHASES:
        state = random_position(seed=100 + stones, stones=stones)
        _turn, stats = engine.find_turn(state, seconds)
        rows.append((name, state.empty_count, stats.completed, stats.nodes,
                     stats.elapsed, stats.nps, stats.turn))
        turn = " ".join(CELL_NAMES[c] for c in stats.turn)
        print("  %-8s  empty=%2d  depth=%d  %7d nodes in %5.2fs  %7.0f n/s  %s"
              % (name, state.empty_count, stats.completed, stats.nodes,
                 stats.elapsed, stats.nps, turn))
    return rows


def bench_depth_by_phase(seconds=2.0):
    """How deep iterative deepening gets in each phase of the game."""
    print("\n== depth reached by phase (%.1fs / position) ==" % seconds)
    for name, stones in PHASES:
        depths = []
        nodes = []
        for seed in range(4):
            state = random_position(seed=200 + stones + seed, stones=stones)
            engine = _engine()
            _turn, stats = engine.find_turn(state, seconds)
            depths.append(stats.completed)
            nodes.append(stats.nodes)
        print("  %-8s  depths %s   mean %.1f   mean nodes %d"
              % (name, depths, sum(depths) / len(depths),
                 sum(nodes) // len(nodes)))


def _self_play(white, black, time_each, max_moves=80):
    """Play one game.  Returns (result, white_nodes, black_nodes, ply)."""
    state = GameState()
    white.new_game()
    black.new_game()
    engines = (white, black)
    nodes = [0, 0]
    moves = 0
    while not state.is_over and moves < max_moves:
        mover = state.to_move
        stats = _play_turn(state, engines[mover], time_each)
        nodes[mover] += stats.nodes
        moves += 1
    return state.compare_final(), nodes[WHITE], nodes[BLACK], moves


def bench_ab_matches(seconds=0.4, games=6):
    """Self-play A/B: full engine versus one enhancement stripped out.

    Each pairing is played from both sides so the first-move advantage does
    not decide the comparison on its own.
    """
    print("\n== A/B self-play (%.2fs / turn, %d games / pairing) ==" % (
        seconds, games))
    full = dict(use_tt=True, use_pvs=True, use_killers=True,
                use_history=True, use_aspiration=True, use_lmr=True,
                use_rfp=True, use_lmp=True, use_iid=False)
    pairings = [
        ("full vs no TT",          full, dict(full, use_tt=False)),
        ("full vs no PVS",         full, dict(full, use_pvs=False)),
        ("full vs no killers",     full, dict(full, use_killers=False)),
        ("full vs no history",     full, dict(full, use_history=False)),
        ("full vs no aspiration",  full, dict(full, use_aspiration=False)),
        ("full vs no LMR",         full, dict(full, use_lmr=False)),
        ("full vs no RFP",         full, dict(full, use_rfp=False)),
        ("full vs no LMP",         full, dict(full, use_lmp=False)),
        ("full vs IID",            full, dict(full, use_iid=True)),
        ("full vs no gift",        full, dict(full, w_gift=0)),
        ("full vs tempo=110",      full, dict(full, w_tempo=110, w_gift=0)),
        ("full vs tempo=40",       full, dict(full, w_tempo=40)),
    ]
    for label, cfg_a, cfg_b in pairings:
        wins = [0, 0, 0]          # A wins, draws, B wins
        for i in range(games):
            a = _engine(**cfg_a)
            b = _engine(**cfg_b)
            if i % 2 == 0:
                result, *_ = _self_play(a, b, seconds)
                if result > 0:
                    wins[0] += 1
                elif result < 0:
                    wins[2] += 1
                else:
                    wins[1] += 1
            else:
                result, *_ = _self_play(b, a, seconds)
                if result < 0:
                    wins[0] += 1
                elif result > 0:
                    wins[2] += 1
                else:
                    wins[1] += 1
        print("  %-22s  A %d  draw %d  B %d" % (label, wins[0], wins[1], wins[2]))


def bench_enhancement_nps(seconds=1.0):
    """Same position, one enhancement switched off at a time, compare speed."""
    print("\n== enhancement cost on a mid-game position (%.1fs) ==" % seconds)
    state = random_position(seed=7, stones=24)
    configs = [
        ("full",            {}),
        ("no TT",           dict(use_tt=False)),
        ("no PVS",          dict(use_pvs=False)),
        ("no killers",      dict(use_killers=False)),
        ("no history",      dict(use_history=False)),
        ("no aspiration",   dict(use_aspiration=False)),
        ("no LMR",          dict(use_lmr=False)),
        ("no RFP",          dict(use_rfp=False)),
        ("no LMP",          dict(use_lmp=False)),
        ("IID on",          dict(use_iid=True)),
        ("no gift",         dict(w_gift=0)),
        ("tempo=110",       dict(w_tempo=110, w_gift=0)),
        ("tempo=40",        dict(w_tempo=40)),
        ("plain alpha-beta", dict(use_tt=False, use_pvs=False, use_killers=False,
                                  use_history=False, use_aspiration=False,
                                  use_lmr=False)),
    ]
    for name, overrides in configs:
        engine = _engine(**overrides)
        _turn, stats = engine.find_turn(state.clone(), seconds)
        print("  %-16s  depth %d  %7d nodes  %7.0f n/s  value %+d"
              % (name, stats.completed, stats.nodes, stats.nps, stats.value))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--quick", action="store_true",
                        help="shorter budgets, fewer games")
    args = parser.parse_args(argv)

    started = time.perf_counter()
    if args.quick:
        bench_nps(0.6)
        bench_depth_by_phase(0.8)
        bench_enhancement_nps(0.6)
        bench_ab_matches(0.15, games=2)
    else:
        bench_nps(2.0)
        bench_depth_by_phase(2.5)
        bench_enhancement_nps(1.5)
        bench_ab_matches(0.35, games=4)
    print("\nfinished in %.1fs  (board has %d cells)" % (
        time.perf_counter() - started, NUM_CELLS))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
