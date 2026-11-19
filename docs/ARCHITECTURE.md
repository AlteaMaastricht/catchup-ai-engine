# Architecture

This document describes the engine as it is implemented in `src/`. File and
function names are given so every statement can be checked in the code.

```
            +-----------------------------+
 Python     |  main.py, catchup/gui.py    |  Tkinter board, console UI, game records
 front-end  |  catchup/{state,search}.py  |  thin wrappers, no game logic
            +--------------+--------------+
                           | ctypes (catchup/_lib.py mirrors include/catchup/c_api.h)
            +--------------v--------------+
 C ABI      |  src/c_api.cpp              |  opaque handles, error codes, no exceptions
            +--------------+--------------+
                           |
            +--------------v--------------+      +---------------------------+
 C++17 core |  src/search.{hpp,cpp}       | ---> | src/state.{hpp,cpp}       |
 (static    |  Engine, Clock              |      | GameState, canonical key  |
  library)  +-----------------------------+      +-------------+-------------+
                                                               |
                                                 +-------------v-------------+
                                                 | src/board.hpp             |
                                                 | geometry, Zobrist, D6 sym |
                                                 +---------------------------+
 Executables: catchup (console), catchup-bench, catchup-match (Elo/SPRT), catchup-test
```

## 1. State representation (`src/board.hpp`, `src/state.{hpp,cpp}`)

* **Board.** 61 hex cells (side 5) indexed 0..60 in row order (`CELL_NAMES`,
  `CELL_X/CELL_Y`). Neighbour lists and 64-bit neighbour masks are compile-time
  tables. The 12 symmetries of the hexagon (D6: 6 rotations x reflection) are a
  `constexpr` permutation table `SYM.perm[12][61]` built from an axial-coordinate
  rotation and reflection about the centre cell E5.
* **Bitboards.** One `uint64_t` per colour (`bb[2]`) plus a `colour[61]` byte array.
* **Incremental union-find.** `parent/size` with per-group liberty masks
  (`lib_root`), group-size histograms (`size_n`), largest/second-largest group,
  sum of squared group sizes, centrality and frontier counters, and `touch`/`join`
  masks (empty cells adjacent to one / several own groups). All are updated by
  `place()` and restored exactly by `unplace()` from fixed-size undo stacks
  (`_u_*`, `_t_*`, `_lib_idx/_lib_old`).
* **Turn structure.** Turns are split into single-stone plies: `place(cell)`
  adds a stone, `end_turn()` passes the move to the opponent and decides whether
  the next turn may place up to 3 stones (`allows_three`).
* **Debug self-check.** In non-`NDEBUG` builds every `place`/`end_turn` calls
  `sanity_check()`, which recomputes all incremental fields the slow way and
  throws on any mismatch.

## 2. Search pipeline (`src/search.cpp`)

`Engine::find_turn` (search.cpp:2215) drives everything:

1. **Opening book** (optional): first stone on the empty board goes to the centre.
2. **Iterative deepening** from depth 1, with a time check every 2048 nodes that
   throws `TimeoutException`; the last completed depth is kept. A "stable-stop"
   rule ends the loop early when the best first stone and value stop changing.
3. **Aspiration windows** (`_search_root`): window seeded from position
   volatility, widened x4, then a full-window fallback.
4. **Negamax with fail-soft alpha-beta** (`_negamax`, search.cpp:1627). A ply is
   either one stone or `END_TURN`; the sign flips only on `END_TURN`. Within a
   turn, stones are generated in increasing cell index (`min_index`) so the same
   set of stones is not searched in several orders; a documented exception lets
   lower-index "bridge" cells through (search.cpp:1029-1034).
5. **Node pipeline**, in order: terminal / decided-race checks -> endgame
   table lookup (<= 10 empty cells) -> TT probe and cutoff -> reverse futility
   pruning (depth <= 4) -> null-move pruning (pass the whole turn, no double null)
   -> enhanced transposition cutoff (`_etc_cutoff`) -> optional IID (off by
   default) -> move generation -> late-move pruning -> PVS move loop with LMR,
   singular-style, gift-turn and critical-gain extensions -> TT store.
6. **Move ordering** (`_fill_moves`, search.cpp:1003): hash move first, then five
   classified buckets (merge / block / expand / harass / quiet) sorted by a
   "swing" key, killers (2 per ply), butterfly history indexed by side,
   placement-in-turn and gift state, countermove and counter-history.
7. **Symmetry reduction at the root of each turn.** When `placed_this_turn == 0`
   and the position is invariant under some symmetries (`_live_symmetries`), only
   one cell per orbit is generated (`_is_canonical_cell`).
8. **Quiescence.** At depth 0 the search resolves 1-ply merge threats before
   standing pat (`best_placement_gain`).
9. **Exact region.** With <= 10 empty cells (or <= 12 with few tactical cells,
   `exact_solve_position`) pruning, reductions and the depth horizon are disabled,
   so the search solves the position.
10. **Result sanitising** (`_sanitise_turn`, search.cpp:2073): the returned turn is
    filtered for legality (occupied/duplicate cells dropped) and completed from the
    TT or move generator if needed.
11. **Evaluation** (`_evaluate`, search.cpp:784): 15 integer-weighted terms
    (largest group, sum of squares, frontier, centrality, tempo, pending stones,
    liberties, stone count, second group, join potential, critical cells,
    third/fourth group, gift penalty, reachable size), with soft saturation.
12. **Time management** (`Clock::allocate`, search.cpp:91): per-turn budget from
    remaining time, game phase, stones per cycle and position volatility.

## 3. Transposition table

* **Key.** `get_canonical_key` (state.cpp:11) takes the minimum occupancy Zobrist
  key over the 12 symmetries, then XORs in side to move, `allows_three`, stones
  placed this turn, score at turn start and `min_index`.
* **Table.** Two slots per bucket (`tt_d` depth-preferred, `tt_a`
  always-replace), `2^tt_bits` buckets (default 20, range 12..24), a generation
  byte so `new_game()` invalidates old entries without clearing, the upper key
  bits as checksum, mate scores stored ply-relative (`score_to_tt`).
  Entries carry up to 3 cells of the principal variation as a packed int.
* **Endgame table.** Exact results with <= 10 empty cells are additionally kept in
  an `unordered_map` keyed by the same canonical key (`_store`, search.cpp:1607).
* **Symmetry and frames** (fixed during preparation, see `docs/PROVENANCE.md` 5.1).
  * At the start of a turn (`min_index == 0`) mirror images share an entry.
  * Inside a turn the key uses the identity frame, because the `min_index`
    restriction is not symmetry-invariant.
  * PV/hash-move cells are stored in the canonical frame of the key (`pack_to_canonical`) and mapped
    back into the probing position's frame (`pack_from_canonical`).
  * `tests/test_reference.cpp` checks this against a plain reference search on
    small positions.
* The table uses per-bucket spin locks although search is single-threaded
  ("Lazy SMP is deferred", search.hpp:150).

## 4. C ABI (`include/catchup/c_api.h`, `src/c_api.cpp`)

* Opaque handles: `CatchupState`, `CatchupEngine`, `CatchupClock`.
* Plain structs for bulk data: `CatchupStateData` (the full incremental state,
  used by the Python wrapper to mirror fields), `CatchupBoardTables`,
  `CatchupEngineConfig` (feature switches and the 15 evaluation weights),
  `CatchupSearchStats`.
* Error handling: no C++ exception crosses the boundary. Mutating calls
  (`place`, `unplace`, `end_turn`, `unend_turn`, `undo_turn`, `play_turn`,
  `engine_find_turn`) return `CATCHUP_OK`/`CATCHUP_ERR`; constructors return
  `NULL` on failure. (These guards were added during repository preparation; see
  `docs/PROVENANCE.md`.)
* `catchup_abi_sizes()` reports the C `sizeof` of the four structs;
  `catchup/_lib.py` compares them with its hand-written `ctypes` mirrors on load
  and refuses to run on a mismatch.
* Library lookup in Python (`catchup/_lib.py`): `$CATCHUP_LIB` if set, otherwise
  `catchup/`, `build/`, `build/Release/`, `build/Debug/`.

## 5. Tools

* `catchup-bench` (`src/bench.cpp`) / `bench.py`: nodes per second by game phase,
  depth reached by phase, cost of each enhancement on one position, TT size, and
  very short A/B self-play.
* `catchup-match` (`src/match.cpp`): multi-threaded self-play between two engine
  configurations over a frozen, hash-checked opening set, with Elo estimate,
  confidence interval and a Wald SPRT.
* `tools/tune.py`: Texel-style logistic tuning of the evaluation weights from
  self-play positions.
