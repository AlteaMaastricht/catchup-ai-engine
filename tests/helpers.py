"""Shared helpers: reference implementations and random position generators.

The reference routines here deliberately recompute everything the slow, obvious
way so that the incremental machinery in the engine has something independent to
be checked against.
"""

import random

from catchup import board as B
from catchup.state import GameState


def random_position(seed, stones=None, stop_before_end=True):
    """Play random legal turns until the board holds ``stones`` stones."""
    rng = random.Random(seed)
    state = GameState()
    target = B.NUM_CELLS if stones is None else stones
    while not state.is_over and (B.NUM_CELLS - state.empty_count) < target:
        count = rng.randint(1, state.max_this_turn)
        empty = [c for c in range(B.NUM_CELLS) if not state.colour[c]]
        state.play_turn(rng.sample(empty, count))
        if stop_before_end and state.is_over:
            break
    return state


def random_playout(seed):
    """Yield ``(state, stones_per_turn)`` for a complete random game."""
    rng = random.Random(seed)
    state = GameState()
    history = []
    while not state.is_over:
        count = rng.randint(1, state.max_this_turn)
        empty = [c for c in range(B.NUM_CELLS) if not state.colour[c]]
        state.play_turn(rng.sample(empty, count))
        history.append(count)
    return state, history


def groups_by_flood_fill(state, player):
    """Group sizes for ``player``, found with a plain flood fill."""
    stone = B.STONE[player]
    colour = state.colour
    seen = [False] * B.NUM_CELLS
    sizes = []
    for start in range(B.NUM_CELLS):
        if colour[start] != stone or seen[start]:
            continue
        stack = [start]
        seen[start] = True
        count = 0
        while stack:
            cell = stack.pop()
            count += 1
            for neighbour in B.NEIGHBOURS[cell]:
                if not seen[neighbour] and colour[neighbour] == stone:
                    seen[neighbour] = True
                    stack.append(neighbour)
        sizes.append(count)
    sizes.sort(reverse=True)
    return sizes


def _largest_liberties(state, player, sizes):
    if not sizes:
        return 0
    target = sizes[0]
    stone = B.STONE[player]
    colour = state.colour
    parent = state.parent
    size = state.size
    seen = 0
    for cell in range(B.NUM_CELLS):
        if colour[cell] != stone:
            continue
        root = cell
        while parent[root] != root:
            root = parent[root]
        if size[root] != target:
            continue
        for n in B.NEIGHBOURS[cell]:
            if not colour[n]:
                seen |= 1 << n
    return seen.bit_count()


def recompute(state, player):
    """Recompute every accumulator for ``player`` from the raw board."""
    stone = B.STONE[player]
    colour = state.colour
    sizes = groups_by_flood_fill(state, player)
    return {
        "groups": sizes,
        # Both scores count as 1 before a player has any group.
        "largest": max(sizes) if sizes else 1,
        "sumsq": sum(s * s for s in sizes),
        "nstones": sum(sizes),
        "centrality": sum(B.CENTRALITY[c] for c in range(B.NUM_CELLS)
                          if colour[c] == stone),
        "frontier": sum(1 for c in range(B.NUM_CELLS) if colour[c] == stone
                        for n in B.NEIGHBOURS[c] if not colour[n]),
        "adjacency": [sum(1 for n in B.NEIGHBOURS[c] if colour[n] == stone)
                      for c in range(B.NUM_CELLS)],
        "second": sizes[1] if len(sizes) > 1 else 0,
        "liberties": _largest_liberties(state, player, sizes),
    }


def recompute_zobrist(state):
    """Rebuild the Zobrist key from scratch."""
    key = 0
    for cell in range(B.NUM_CELLS):
        value = state.colour[cell]
        if value:
            key ^= B.ZOBRIST_CELL[value - 1][cell]
    if state.to_move == B.BLACK:
        key ^= B.ZOBRIST_SIDE
    if state.allows_three:
        key ^= B.ZOBRIST_ALLOWS_THREE
    return key
