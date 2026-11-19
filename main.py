#!/usr/bin/env python3
"""Entry point for the Catchup engine.

    python3 main.py                 choose a side from the menu
    python3 main.py --white         play White against the engine
    python3 main.py --black         play Black against the engine
    python3 main.py --self-play     watch the engine play itself
    python3 main.py --time 600      seconds each side gets for the whole game
    python3 main.py --gui           visual hex board (play against the engine)

    cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
    ./build/catchup --white         native C++ console player
    ./build/catchup-test            C++ unit tests
    ./build/catchup-bench --quick   native benchmarks

Game logic and search live in C++ (libcatchup).  The Python package is a
ctypes wrapper so the Tkinter GUI can keep working.  Build the library
before ``--gui``.
"""

import argparse
import sys

from catchup.board import BLACK, WHITE
from catchup.search import Engine
from catchup.ui import play

MENU = """\
Catchup
  1  play White (you move first)
  2  play Black (the engine moves first)
  3  watch the engine play itself
  4  visual board
  5  quit
"""


def choose_side():
    while True:
        print(MENU)
        try:
            answer = input("  choice > ").strip()
        except EOFError:
            return "quit"
        if answer == "1":
            return WHITE
        if answer == "2":
            return BLACK
        if answer == "3":
            return None
        if answer == "4":
            return "gui"
        if answer in ("5", "q", "quit"):
            return "quit"
        print("  Please type 1, 2, 3, 4 or 5.\n")


def main(argv=None):
    parser = argparse.ArgumentParser(description="Play Catchup against an alpha-beta engine.")
    side = parser.add_mutually_exclusive_group()
    side.add_argument("--white", action="store_true", help="play as White")
    side.add_argument("--black", action="store_true", help="play as Black")
    side.add_argument("--self-play", action="store_true", help="engine versus engine")
    parser.add_argument("--time", type=float, default=600.0,
                        help="seconds per side for the whole game (default 600)")
    parser.add_argument("--quiet", action="store_true",
                        help="do not print the engine's search statistics")
    parser.add_argument("--gui", action="store_true",
                        help="open the visual board instead of the console")
    args = parser.parse_args(argv)

    if args.gui:
        from catchup.gui import launch
        launch(total_time=args.time)
        return 0

    if args.white:
        human = WHITE
    elif args.black:
        human = BLACK
    elif args.self_play:
        human = None
    else:
        human = choose_side()
        if human == "quit":
            return 0
        if human == "gui":
            from catchup.gui import launch
            launch(total_time=args.time)
            return 0

    play(human_side=human, engine=Engine(), total_time=args.time,
         verbose=not args.quiet)
    return 0


if __name__ == "__main__":
    sys.exit(main())
