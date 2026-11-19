# Provenance

This repository is a cleaned publication copy of a solo university project
(game-playing engine for Catchup, September-October 2026). The original working tree was
copied file by file; nothing in the original folder was modified. This file
lists every difference between the original and this copy so the original work
and later preparation changes can be told apart.

## 1. Copied unchanged

| Original (project root) | Here |
|---|---|
| `src/*.cpp`, `src/*.hpp` (except `c_api.cpp`, `state.{cpp,hpp}`, `search.{cpp,hpp}`) | `src/` |
| `catchup/__init__.py`, `board.py`, `gui.py`, `record.py`, `ui.py` | `catchup/` |
| `tests/*.cpp` (except `test_main.cpp` and the new `test_reference.cpp`), `tests/__init__.py`, `tests/helpers.py`, `tests/test_gui.py`, `tests/test_record.py`, `tests/test_search.py`, `tests/test_state.py` | `tests/` |
| `tools/tune.py`, `main.py` | same paths |

## 2. Copied and changed during preparation

| File | Change | Why |
|---|---|---|
| `src/c_api.cpp` | Every `extern "C"` entry that can throw is wrapped in `try/catch` and returns `CATCHUP_ERR` / `nullptr` / `0.0`. Null-pointer checks added. `catchup_state_place` rejects off-board and occupied cells. `unplace`/`unend_turn`/`undo_turn` refuse to run on an empty undo stack (before: out-of-bounds index `-1`). `catchup_engine_new` now catches `std::bad_alloc` from the TT allocation (`new (std::nothrow)` only covered the outer object). `catchup_engine_evaluate` restores the engine's previous state pointer instead of leaving it pointing at the caller's state. New `catchup_abi_sizes()`. | C++ exceptions crossing a C boundary into ctypes are undefined behaviour; several entries could also read out of bounds on misuse. Search code is untouched. |
| `include/catchup/c_api.h` | `place`, `unplace`, `end_turn`, `unend_turn`, `undo_turn` return `int` instead of `void`; `catchup_abi_sizes` declared; error-handling comment. | Needed to report the errors above. Callers that ignore the return value are unaffected. |
| `catchup/_lib.py` | `CATCHUP_LIB` environment variable overrides the library search; struct-size check against `catchup_abi_sizes()` on load; `restype = c_int` for the functions above. | Allows testing an out-of-tree build (CI, sanitizer builds); detects drift between the hand-written ctypes structs and the C header. |
| `catchup/state.py` | `place`/`unplace`/`end_turn`/`unend_turn`/`undo_turn` raise `IllegalMove`/`RuntimeError` when the C call returns an error. | Surfaces the new error codes. |
| `catchup/search.py` | `TT_BITS = 22` -> `20`; `find_turn` raises on an error code. | **Bug fix.** C++ (`src/search.hpp:25`), the C API defaults and the Python test `tests/test_record.py:60` all use 20; the Python constant had drifted to 22, so the GUI allocated a table 4x the documented size and `test_writes_jsonl_under_seconds_folder` failed on the original code (`AssertionError: 22 != 20`). |
| `CMakeLists.txt` | Options `CATCHUP_LIB_IN_PACKAGE` (default `ON` = original behaviour: library written into `catchup/`) and `CATCHUP_SANITIZE` (ASan + UBSan); MSVC warning flags; `enable_testing()` with `ctest` targets for `catchup-test` and the Python suite. | Out-of-tree builds and CI; the original sanitizer build was configured by hand. |
| `src/state.cpp`, `src/state.hpp` | **Phase 2 bug fix (TT symmetry).** `get_canonical_key` uses only the identity frame when `min_index > 0`. It also gains an overload that reports the minimising symmetry `g`, and there is a new `sym_inverse_cell()`. | Mid-turn mirror positions were sharing keys although their allowed moves differ (section 5.1). |
| `src/search.cpp`, `src/search.hpp` | **Phase 2 bug fix (TT symmetry).** PV/hash-move cells in TT and endgame-table entries are now stored in the canonical frame (`pack_to_canonical`) and mapped back into the probing position's frame (`pack_from_canonical`): in `_negamax` (TT hit, endgame table, IID re-probe), in `_store` (new `sym` argument, default 0) and in `_sanitise_turn`. `tt_lookup` converts back when the key is the current state's turn-start key. `_negamax` computes the key lazily, as before, and only once per node. | Turn-start mirror positions share a key by design, but the stored moves were in the storing position's coordinates. The engine then played losing turns in won positions (section 5.1). Search rules, pruning and evaluation are unchanged. |
| `tests/check.hpp`, `tests/test_main.cpp`, `CMakeLists.txt` | Register `run_reference_tests()` and add `tests/test_reference.cpp` to `catchup-test`. | Phase 2 oracle tests. |
| `bench.py` | Module docstring reworded (no reference to the course hand-in). | Neutral wording. |
| `tests/test_board.py`, `tests/test_rules.py`, `tests/test_ui.py` | Comments / two test-method names reworded from "assignment" to "rules description". Test bodies unchanged. | Neutral wording. |
| `.gitignore` | Extended (all `build*/` dirs, venvs, IDE folders). | The original listed only `/build` and `/build-asan`. |

## 3. Added during preparation

* `tests/test_c_api.py` - 5 tests for the C ABI guards and the struct-size check.
* `tests/test_reference.cpp` (Phase 2) - a plain reference search, plus three checks:
  * the reference itself has no alpha-beta, TT, symmetry, ordering or pruning, and uses the engine's own rules;
  * a rules check that the position after a turn does not depend on the order of its stones;
  * a mirror-key regression (2,440 mirror pairs whose allowed moves differ must get different keys);
  * an oracle comparison of engine values and chosen turns against the reference on 8-empty positions.
  `CATCHUP_ORACLE_FULL=1` adds 9-empty positions and a symmetry check of the reference itself.
* `README.md` (replaces the original hand-in README), `docs/ARCHITECTURE.md`,
  this file, `.github/workflows/ci.yml` (not yet run on GitHub).

## 4. Not copied

| Item | Reason |
|---|---|
| Hand-in folder and its zip archives | Duplicate of the code; contain a student ID in their names. |
| Presentation video (`.mp4`, ~83 MB) | Personal media, large binary. |
| Design/testing report PDF | Needs rewriting before publication (contains AI-generated reviewer artefacts); `docs/ARCHITECTURE.md` was written from the code instead. |
| Course rules/specification PDF, slides | Course material, not redistributable. |
| `catchup/libcatchup.dylib`, `build*/`, `.venv*`, `__pycache__/`, `.DS_Store` | Build output / environments. |
| `games/` | Personal game logs (human vs engine sessions). |
| `README.md` (original) | Written for the hand-in (Windows/PyPy instructions for the grader). |
| `build-windows.bat`, `run-pypy.bat` | One-line wrappers around the commands documented in the README. |
| Earlier prototype, experimental branch, backups (sibling folders) | Superseded versions. |

## 5. Issues found during preparation

### 5.1 TT symmetry handling (fixed in Phase 2; line numbers in this subsection refer to the original code)

`get_canonical_key` (`src/state.cpp:11-37`) minimises the occupancy key over the
12 board symmetries but XORs in `ZOBRIST_MIN_INDEX[min_index]` without
transforming it. Inside a turn, `min_index` restricts which cells may be placed
next (`INDEX_MASK_FROM[min_index]`, `src/search.cpp:1025`), and that index range
is not symmetry-invariant. So two mirror-image positions with the same
`min_index` get the same key (`_tt_key`, search.cpp:1384; the endgame table at
search.cpp:1642 and 1609 uses the same key) although the sets of stones they
may still place are not images of each other.

Measurement (probe program written for this check, not part of the repo; positions
"White plays w, Black has placed one stone on a cell b fixed by symmetry g", compared
with "White plays g(w), Black on b", with `min_index = b + 1`):

```
key collisions between distinct mirror-image mid-turn positions: 2544
  of which the restricted move sets are NOT images of each other: 2440
  of which a depth-1 search (TT off) returns a different value:   120
  control - same pairs with no min_index restriction, values differ: 26
```

The control shows that the evaluation/search itself is not perfectly
symmetry-invariant (26 pairs differ even without the restriction), so not all
120 value differences are caused by the key; the 2440 colliding pairs with
non-equivalent move sets are the direct evidence that the key merges nodes
that are not equivalent.

**What the oracle found on the unfixed code.** `tests/test_reference.cpp` was run with
`CATCHUP_ORACLE_FULL=1` before the fix. Every position was solved to the end and checked
against the plain reference. One engine (shared TT and endgame table) solves all 12 mirror
images of each position.

| Case | Positions | Value mismatches | Suboptimal turns (chosen turn loses a won position) |
|---|---|---|---|
| turn start, 8 empty | 144 | 0 | 12 |
| mid-turn, 8 empty | 144 | 0 | 0 |
| turn start, 9 empty | 72 | 0 | 20 |
| mid-turn, 9 empty | 72 | 0 | 0 |

Also on the unfixed code:
* The mirror-key regression failed for all 2,440 non-equivalent pairs.
* The reference was symmetric in all cases, and order independence held.

The failures came almost only from mirror images after the first: for example, image 0 of
family 2 (9 empty) was correct and images 1-11 were all wrong. The cause is the second half
of this issue. Turn-start keys are shared by mirror images by design, but the stored
PV/endgame-table cells were in the coordinates of the position that stored them. At the
root (the endgame-table lookup has no `ply > 0` guard) the engine loaded a mirror's PV,
`_sanitise_turn` repaired it into a *legal* turn, and that turn lost. Values were never
wrong in these tests. The `min_index` collision is real (2,440 colliding pairs) but stayed
latent here: in exact solves it needs a colliding node in the same table with the same
`min_index`, and a win/draw/loss value that actually differs between the two restricted
move sets.

**Fix (Phase 2).**
1. Inside a turn (`min_index > 0`) the key uses the identity frame only.
2. Moves stored in TT/endgame-table entries are kept in the canonical frame of the key and
   mapped back into the frame of the probing position.

Symmetry sharing at turn start is kept.

**After the fix** (same command, same seeds):
* 0 value mismatches and 0 suboptimal turns in all four cases (432 positions);
* mirror-key regression: 0 failures;
* the full C++ suite passed (`54635 checks passed`), and the Python suite passed (87 tests).

`catchup-bench --quick` was run before and after, back to back on the same machine at load
average ≈2.3 (single runs, `results/bench-quick-2026-11-18-{before,after}-fix.txt`). Node
rates and TT hit counts moved by a few percent in both directions, which is within
run-to-run noise. Example: mid-game TT-size test at 20 bits, 33,492/197,346 hits before and
35,198/210,077 after. These runs measured no consistent performance change.

A theoretical alternative would canonicalise over (board, allowed-move mask) jointly. It was
not used because the allowed set also contains search-dependent "bridge" cells
(search.cpp:1029-1034).

### 5.2 Other points

* `catchup_state_set` does not restore the native undo counters (`_undo_n`, ...).
  The Python wrapper always writes back a mirror of the same native object, so
  this is consistent in practice, but the ABI allows inconsistent states.
* The Python wrapper copies the whole state across the boundary (`_push`/`_pull`)
  on every operation.
* `Engine` takes up to 32 positional constructor arguments; `_negamax` is about
  410 lines.
* The endgame `unordered_map` is not cleared by `new_game()`.
* The Python `report` callback of `Engine.find_turn` fires once after the search,
  not per completed depth.
* Several C++ tests use short wall-clock budgets (`find_turn(state, 0.01)`) and may
  be timing-sensitive on slow CI machines.

## Phase 4 changes (18 Nov 2026)
- `LICENSE` added: MIT, copyright 2026 Arthur J. Maarschalkerweerd (author decision D6). The README states that it covers the code, not the game.
- README rules credit: the designer is Nick Bentley (2010, free print-and-play). The BoardGameGeek link uses game ID 68199, taken from two board-game directory mirrors in a web search on 18 Nov 2026; the BGG page itself was not opened. Re-check the link before publishing.
