# catchup-ai-engine

A game-playing engine for the hex stone-placement game **Catchup**: a C++17
alpha-beta search (bitboards, incremental union-find, symmetry-aware
transposition table, PVS / null-move / LMR, exact endgame solving) with a C ABI
and a small Python/Tkinter front-end.

## 1. Overview

* **What it is:** an engine that picks a full turn (1-3 stones) for any Catchup
  position under a time limit, plus a console player, a Tkinter board, a
  benchmark, a self-play match runner (Elo / SPRT) and a weight tuner.
* **Size:** about 6.3k lines of C++ engine code (incl. the C header), 3.5k lines of C++ tests, and a
  stdlib-only Python package (`catchup/`) that wraps the engine through `ctypes`.
* **State:** the engine, front-ends and tools are implemented and build cleanly
  (no compiler warnings with `-Wall -Wextra -Wpedantic`). The C++ and Python test
  suites pass, including an oracle test against a plain reference search. A TT
  symmetry bug found during preparation was fixed (see section 9 and `docs/PROVENANCE.md`).

## 2. Problem

Catchup is played on a hexagonal board with 61 cells (side 5). Players take
turns placing stones of their colour. A player's score is the size of their
largest connected group. White's first turn is a single stone; after that a
turn is 1 or 2 stones, or up to 3 if the previous player increased their score
on their turn and now has at least as large a score as the player to move. When the board is full,
the larger largest group wins; ties are broken by comparing the next-largest
groups. The rules are those of the abstract game Catchup, designed by Nick Bentley
(2010, free print-and-play), listed on
[BoardGameGeek](https://boardgamegeek.com/boardgame/68199/catchup); the summary above is a
paraphrase.

What makes it awkward for a standard game-tree search:

* a turn is a variable number of placements, so the branching factor per turn
  is in the thousands early on;
* the "catch-up" rule makes extra stones a gift to the opponent, so greedy
  growth is often wrong;
* the evaluation is about connectivity, which changes a lot with one stone.

## 3. Approach

* **Turn decomposition.** A turn is searched as single-stone plies ending in
  `END_TURN`; stones inside a turn are generated in increasing cell index so a
  set of stones is not searched in several orders.
* **Search.** Negamax with fail-soft alpha-beta, iterative deepening with a
  time check every 2048 nodes, aspiration windows, principal-variation search,
  reverse futility pruning, null-move pruning (pass the whole turn), late-move
  pruning, log-based late-move reductions, singular-style / gift-turn /
  critical-gain extensions, a merge-threat quiescence step, and exact solving
  with <= 10 empty cells (<= 12 when few cells are tactical).
* **Move ordering.** Hash move, five move classes (merge / block / expand /
  harass / quiet) sorted by a swing estimate, two killers per ply, history with
  gravity, countermove and counter-history.
* **Symmetry.** Transposition-table keys are the minimum Zobrist key over the
  12 symmetries of the hexagon; at the start of a turn, symmetric cells are
  generated once.
* **Evaluation.** 15 integer-weighted terms (largest/second/third/fourth group,
  sum of squares, liberties, reachable size, join potential, critical cells,
  gift penalty, ...) with soft saturation.
* **Time management.** Budget per turn from remaining time, phase, stones per
  cycle and position volatility.

## 4. Architecture

```
Python (main.py, catchup/gui.py, catchup/ui.py)
   |  ctypes  (catchup/_lib.py mirrors include/catchup/c_api.h, size-checked on load)
C ABI  src/c_api.cpp          opaque handles + error codes, no exceptions cross it
   |
C++17 core (static lib)       src/search.{hpp,cpp}   Engine, Clock, evaluation
                              src/state.{hpp,cpp}    GameState, canonical key
                              src/board.hpp          geometry, Zobrist, D6 tables
Executables                   catchup, catchup-bench, catchup-match, catchup-test
```

Details (state representation, node pipeline, TT layout, C ABI):
[`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md).

```
.
├── CMakeLists.txt
├── include/catchup/c_api.h   C interface
├── src/                      engine, console player, bench, match runner
├── catchup/                  Python package (ctypes wrapper, Tk GUI, console UI, game records)
├── tests/                    C++ CHECK harness + Python unittest suite
├── tools/tune.py             Texel-style weight tuning
├── bench.py, main.py         Python bench and launcher
├── results/                  saved benchmark output (with machine info)
├── docs/                     ARCHITECTURE.md, PROVENANCE.md
└── .github/workflows/ci.yml  CI definition (not yet run)
```

## 5. Technologies

C++17 (standard library only; `<thread>` in the match runner), CMake >= 3.16,
Python >= 3.10 standard library (`ctypes`, `tkinter` for the GUI, `unittest`).
No third-party packages. (The front-end was also used under PyPy during
development; that was not re-tested here.)

## 6. Installation & running

Requirements: a C++17 compiler (clang, gcc or MSVC), CMake >= 3.16, Python 3.10+.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

By default the shared library is written into `catchup/` (`libcatchup.so`,
`libcatchup.dylib` or `catchup.dll`) so the Python package finds it. To keep it
in the build directory instead, configure with `-DCATCHUP_LIB_IN_PACKAGE=OFF`;
the loader also searches `build/`, `build/Release/`, `build/Debug/`, or set
`CATCHUP_LIB=/path/to/libcatchup.so`.

Run:

```sh
./build/catchup --white --time 600     # console game, you play White, 10 min each
python3 main.py --gui --time 600       # Tkinter board
python3 main.py --self-play --time 60  # engine vs engine in the console
```

Tests:

```sh
ctest --test-dir build --output-on-failure          # C++ checks + Python suite
./build/catchup-test                                # C++ only
python3 -m unittest discover -s tests -t .          # Python only
```

The default C++ run includes a quick oracle subset (about 6 s). `CATCHUP_ORACLE_FULL=1 ./build/catchup-test`
runs the full oracle comparison (about 6 minutes on the machine in section 8).

Sanitizer build (ASan + UBSan):

```sh
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=RelWithDebInfo -DCATCHUP_SANITIZE=ON -DCATCHUP_LIB_IN_PACKAGE=OFF
cmake --build build-asan -j && ctest --test-dir build-asan --output-on-failure
```

A `Debug` build additionally runs the internal `sanity_check()` after every
move, but it is too slow for the time-limited C++ tests: under Debug + ASan
some of them fail or abort on their wall-clock deadlines (see section 9).

## 7. Example usage

From Python:

```python
from catchup.state import GameState
from catchup.search import Engine
from catchup.board import CELL_NAMES

state = GameState()
state.play_turn([30])                      # White opens on E5
engine = Engine(use_opening_book=False)
turn, stats = engine.find_turn(state, 1.0) # Black, 1 second
print([CELL_NAMES[c] for c in turn], stats.describe())
```

Benchmark and engine-vs-engine matches:

```sh
./build/catchup-bench --quick
./build/catchup-match --games 400 --time 0.5 --sprt --elo0 0 --elo1 10   # long run
```

## 8. Results

Only results that were re-run while preparing this repository are reported.
All numbers are single-machine measurements (Intel Core i5-5350U @ 1.80 GHz,
2 cores / 4 threads, macOS 12, Apple clang 14, Release build) and vary from run to
run because the searches are time-limited.

**Tests** (local run, 17 Nov 2026):

| Suite | Result |
|---|---|
| `catchup-test` (Release) | passed: 54,623-54,625 checks in three runs (the count varies slightly with timing; it includes checks inside loops over generated positions and the oracle below) |
| `catchup-test` (RelWithDebInfo + ASan/UBSan) | passed (54,625 checks), no sanitizer reports |
| `catchup-test` (Debug + ASan/UBSan) | failed on time-limited tests, no sanitizer reports (run before the oracle tests were added; see section 9) |
| Python `unittest` | 87 tests passed (82 original + 5 added for the C ABI) |

**Oracle test** (`tests/test_reference.cpp`, `CATCHUP_ORACLE_FULL=1`). Every
position is solved to the end and compared with a plain reference search (no
alpha-beta, TT, symmetry, ordering or pruning). One engine solves all 12 mirror
images of each position:

| Case | Positions | Value mismatches | Losing turn chosen in a won position |
|---|---|---|---|
| turn start, 8 / 9 empty cells | 144 / 72 | 0 / 0 | 0 / 0 (before the fix: 12 / 20) |
| mid-turn, 8 / 9 empty cells | 144 / 72 | 0 / 0 | 0 / 0 |

**`catchup-bench --quick`** (one run, load average ≈2.3; full output in
[`results/bench-quick-2026-11-18-after-fix.txt`](results/bench-quick-2026-11-18-after-fix.txt)):

| Phase (random position) | Empty cells | Depth reached in ~0.6 s | Nodes/s |
|---|---|---|---|
| opening | 61 | 5 | 443,288 |
| early | 48 | 4 | 271,574 |
| midgame | 33 | 3 | 281,987 |
| late | 13 | 1 | 427,550 |

Depth reached at 0.8 s per position (4 positions per phase): mean 5.0 / 3.8 /
3.2 / 2.0 for opening / early / midgame / late. A 17 Nov run under unrecorded
parallel load gave roughly 3x lower node rates
([`results/bench-quick-2026-11-17.txt`](results/bench-quick-2026-11-17.txt));
this shows how sensitive these numbers are to machine load.
The single-feature ablations on one midgame position reach depth 3-4 with
values +374 / +403. That is one short run, not a measurement of feature value.

Not measured here: playing strength against other programs or humans, and
statistically meaningful Elo differences between search features. The short
A/B self-play in the bench (2 games per pairing) is a smoke test, not evidence of
strength; `catchup-match` with `--sprt` is the intended tool and has not been run
to completion.

## 9. Limitations

* **TT symmetry (fixed during preparation).** Mirror images shared TT entries
  with moves stored in the wrong frame. The engine then played losing turns in
  won endgame positions (12 of 144 and 20 of 72 oracle cases). Mid-turn keys also
  merged nodes with different allowed moves. Both were fixed and are covered by
  regression tests; see `docs/PROVENANCE.md` section 5.1.
* **Oracle scope.** The reference comparison covers only positions with <= 9
  empty cells, where the engine solves exactly. Heuristic (depth-limited)
  play in larger positions has no oracle.
* **Mid-turn roots.** `find_turn` assumes it is called at the start of a turn
  (it uses `max_this_turn` as the root budget, search.cpp `find_turn`).
* **Unvalidated features.** Several search features (NMP, LMR, history
  variants) and the tuned evaluation weights have not been validated with
  statistically significant self-play.
* **Bridge overhead.** The Python wrapper mirrors the whole native state on
  every call; the ctypes structs are hand-written (now size-checked).
* **Maintainability.** `Engine` has a ~32-argument constructor and a ~410-line
  `_negamax`; most members are public; tuning constants are spread across headers.
* **Single-threaded.** The TT has per-bucket locks but Lazy SMP was not
  implemented.
* **Timing-based tests.** Several C++ tests depend on wall-clock budgets. In a
  Debug + ASan build (which aborted after 148 s, versus 11 s for the
  Release test run) one tactical check fails and a test
  aborts on an uncaught `TimeoutException`; no sanitizer error was reported.
* **CI.** `.github/workflows/ci.yml` is prepared but has never run.

## 10. Reproducibility

* **No data needed.** Positions for tests and benchmarks are generated from
  fixed seeds (`src/position.cpp`, `tests/helpers.py`); the match runner uses a
  frozen opening set with a hash check (`--expect-openings-hash`).
* **Deterministic:** rules, state updates, keys, test positions.
* **Not deterministic:** anything time-limited (search depth, node counts,
  chosen moves, bench and match output) depends on machine speed and load.
* **Not included:** the original design report, game logs and benchmark logs
  from development.

## 11. Context, attribution & licence

* Solo project for a university master's course on search and game-playing
  (2026). The rules are those of the published board game Catchup (see
  section 2); the engine, front-ends and tests are the author's own work. No
  course-provided code is included.
* Changes made while preparing this repository (C ABI guards, a TT-size
  constant fix, build options, docs) are listed in
  [`docs/PROVENANCE.md`](docs/PROVENANCE.md).
* AI assistance:
  - **Original course implementation (2026):** whether and how AI was used is unspecified.
  - **Portfolio preparation:** the C API hardening, minimax reference tests, transposition-table
    symmetry fix, build/CI setup and documentation were done with Claude Code under my direction. I
    reviewed the changes. Details are in [`docs/PROVENANCE.md`](docs/PROVENANCE.md).
* License: MIT — see [`LICENSE`](LICENSE). The licence covers this code only, not the game itself.
