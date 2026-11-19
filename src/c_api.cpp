#include "catchup/c_api.h"

#include "board.hpp"
#include "search.hpp"

#include <algorithm>
#include <cstring>
#include <new>
#include <string>
#include <utility>
#include <vector>

using catchup::Clock;
using catchup::Engine;
using catchup::GameState;

struct CatchupState {
    GameState impl;
};

struct CatchupEngine {
    Engine impl;
    explicit CatchupEngine(const CatchupEngineConfig& cfg)
        : impl(cfg.use_tt != 0, cfg.use_pvs != 0, cfg.use_killers != 0,
               cfg.use_history != 0, cfg.use_aspiration != 0, cfg.use_lmr != 0,
               cfg.use_opening_book != 0, cfg.max_depth, cfg.aspiration_delta,
               cfg.w_largest, cfg.w_sumsq, cfg.w_frontier, cfg.w_centre,
               cfg.w_tempo, cfg.w_pending, cfg.w_liberty, cfg.w_stones,
               cfg.w_second, cfg.w_join, cfg.w_crit, cfg.w_third, cfg.w_fourth,
               cfg.use_rfp != 0, cfg.use_lmp != 0, cfg.use_iid != 0,
               cfg.w_gift, cfg.w_reachable,
               cfg.tt_bits ? cfg.tt_bits : catchup::TT_BITS,
               cfg.use_countermove != 0, cfg.use_stable_stop != 0) {}
};

struct CatchupClock {
    Clock impl;
};

namespace {

void fill_data(const GameState& s, CatchupStateData* out) {
    std::memcpy(out->colour, s.colour, sizeof(out->colour));
    out->bb[0] = s.bb[0];
    out->bb[1] = s.bb[1];
    std::memcpy(out->adjacency, s.adjacency, sizeof(out->adjacency));
    out->touch[0] = s.touch[0];
    out->touch[1] = s.touch[1];
    out->join[0] = s.join[0];
    out->join[1] = s.join[1];
    out->empty_count = s.empty_count;
    std::memcpy(out->parent, s.parent, sizeof(out->parent));
    std::memcpy(out->size, s.size, sizeof(out->size));
    std::memcpy(out->size_n, s.size_n, sizeof(out->size_n));
    std::memcpy(out->lib_root, s.lib_root, sizeof(out->lib_root));
    for (int p = 0; p < 2; ++p) {
        out->largest[p] = s.largest[p];
        out->second[p] = s.second[p];
        out->largest_root[p] = s.largest_root[p];
        out->sumsq[p] = s.sumsq[p];
        out->nstones[p] = s.nstones[p];
        out->centrality[p] = s.centrality[p];
        out->frontier[p] = s.frontier[p];
        out->score_increased[p] = s.score_increased[p] ? 1 : 0;
    }
    out->to_move = s.to_move;
    out->turn_index = s.turn_index;
    out->placed_this_turn = s.placed_this_turn;
    out->max_this_turn = s.max_this_turn;
    out->score_at_turn_start = s.score_at_turn_start;
    out->allows_three = s.allows_three ? 1 : 0;
    out->zobrist = s.zobrist;
    out->undo_n = s._undo_n;
    out->turn_undo_n = s._turn_undo_n;
    out->lib_undo_n = s._lib_undo_n;
}

void apply_data(GameState& s, const CatchupStateData* in) {
    std::memcpy(s.colour, in->colour, sizeof(in->colour));
    s.bb[0] = in->bb[0];
    s.bb[1] = in->bb[1];
    std::memcpy(s.adjacency, in->adjacency, sizeof(in->adjacency));
    s.touch[0] = in->touch[0];
    s.touch[1] = in->touch[1];
    s.join[0] = in->join[0];
    s.join[1] = in->join[1];
    s.empty_count = in->empty_count;
    std::memcpy(s.parent, in->parent, sizeof(in->parent));
    std::memcpy(s.size, in->size, sizeof(in->size));
    std::memcpy(s.size_n, in->size_n, sizeof(in->size_n));
    std::memcpy(s.lib_root, in->lib_root, sizeof(in->lib_root));
    for (int p = 0; p < 2; ++p) {
        s.largest[p] = in->largest[p];
        s.second[p] = in->second[p];
        s.largest_root[p] = in->largest_root[p];
        s.sumsq[p] = in->sumsq[p];
        s.nstones[p] = in->nstones[p];
        s.centrality[p] = in->centrality[p];
        s.frontier[p] = in->frontier[p];
        s.score_increased[p] = in->score_increased[p] != 0;
    }
    s.to_move = in->to_move;
    s.turn_index = in->turn_index;
    s.placed_this_turn = in->placed_this_turn;
    s.max_this_turn = in->max_this_turn;
    s.score_at_turn_start = in->score_at_turn_start;
    s.allows_three = in->allows_three != 0;
    s.zobrist = in->zobrist;
    s.invalidate_reach_cache();
}

CatchupEngineConfig defaults_cfg() {
    CatchupEngineConfig c{};
    c.use_tt = c.use_pvs = c.use_killers = c.use_history = 1;
    c.use_aspiration = c.use_lmr = c.use_opening_book = 1;
    c.max_depth = 64;
    c.aspiration_delta = 120;
    c.w_largest = 100;
    c.w_sumsq = 2;
    c.w_frontier = 1;
    c.w_centre = 2;
    c.w_tempo = 0;
    c.w_pending = 25;
    c.w_liberty = 8;
    c.w_stones = 20;
    c.w_second = 40;
    c.w_join = 4;
    c.w_crit = 16;
    c.w_third = 25;
    c.w_fourth = 8;
    c.use_rfp = c.use_lmp = 1;
    c.use_iid = 0;
    c.w_gift = 80;
    c.w_reachable = 10;
    c.tt_bits = catchup::TT_BITS;
    c.use_countermove = 1;
    c.use_stable_stop = 1;
    return c;
}

}  // namespace

void catchup_board_tables(CatchupBoardTables* out) {
    std::memset(out, 0, sizeof(*out));
    for (int i = 0; i < catchup::NUM_CELLS; ++i) {
        std::strncpy(out->cell_names[i], catchup::CELL_NAMES[i], 3);
        out->cell_x[i] = catchup::CELL_X[i];
        out->cell_y[i] = catchup::CELL_Y[i];
        out->neighbour_count[i] = catchup::NEIGHBOUR_COUNT[i];
        for (int j = 0; j < 6; ++j) out->neighbours[i][j] = catchup::NEIGHBOURS[i][j];
        out->neighbour_mask[i] = catchup::NEIGHBOUR_MASK[i];
        out->ring[i] = catchup::RING[i];
        out->centrality[i] = catchup::CENTRALITY[i];
        out->static_order[i] = catchup::STATIC_ORDER[i];
        out->static_rank[i] = catchup::STATIC_RANK[i];
        out->zobrist_cell[0][i] = catchup::ZOBRIST_CELL[0][i];
        out->zobrist_cell[1][i] = catchup::ZOBRIST_CELL[1][i];
    }
    out->full_mask = catchup::FULL_MASK;
    for (int i = 0; i <= catchup::NUM_CELLS; ++i) {
        out->index_mask_from[i] = catchup::INDEX_MASK_FROM[i];
    }
    out->zobrist_side = catchup::ZOBRIST_SIDE;
    out->zobrist_allows_three = catchup::ZOBRIST_ALLOWS_THREE;
    for (int i = 0; i < 4; ++i) out->zobrist_placed[i] = catchup::ZOBRIST_PLACED[i];
    out->centre_cell = catchup::CENTRE_CELL;
    for (int r = 0; r < catchup::GRID; ++r) {
        out->display_number[r] = catchup::DISPLAY_ROWS[r].number;
        out->display_indent[r] = catchup::DISPLAY_ROWS[r].indent;
        out->display_ncells[r] = catchup::DISPLAY_ROWS[r].ncells;
        for (int i = 0; i < 9; ++i) {
            out->display_cells[r][i] = catchup::DISPLAY_ROWS[r].cells[i];
        }
    }
}

int catchup_parse_cell(const char* text) {
    try {
        return catchup::parse_cell(text ? text : "");
    } catch (...) {
        return CATCHUP_ERR;
    }
}

// Every entry point below is noexcept from the caller's point of view: C++
// exceptions must not cross the extern "C" boundary (that is undefined
// behaviour for a ctypes caller), so they are caught and turned into
// CATCHUP_ERR / nullptr / 0.

void catchup_abi_sizes(int* out4) {
    if (!out4) return;
    out4[0] = static_cast<int>(sizeof(CatchupStateData));
    out4[1] = static_cast<int>(sizeof(CatchupBoardTables));
    out4[2] = static_cast<int>(sizeof(CatchupEngineConfig));
    out4[3] = static_cast<int>(sizeof(CatchupSearchStats));
}

CatchupState* catchup_state_new(void) {
    try {
        return new CatchupState();
    } catch (...) {
        return nullptr;
    }
}

void catchup_state_free(CatchupState* s) { delete s; }

CatchupState* catchup_state_clone(const CatchupState* s) {
    if (!s) return nullptr;
    try {
        auto* copy = new CatchupState();
        copy->impl = s->impl.clone();
        return copy;
    } catch (...) {
        return nullptr;
    }
}

void catchup_state_get(const CatchupState* s, CatchupStateData* out) {
    if (!s || !out) return;
    fill_data(s->impl, out);
}

void catchup_state_set(CatchupState* s, const CatchupStateData* in) {
    if (!s || !in) return;
    apply_data(s->impl, in);
}

int catchup_state_place(CatchupState* s, int cell) {
    if (!s || cell < 0 || cell >= catchup::NUM_CELLS) return CATCHUP_ERR;
    if (s->impl.colour[cell] != catchup::EMPTY) return CATCHUP_ERR;
    try {
        s->impl.place(cell);
        return CATCHUP_OK;
    } catch (...) {
        return CATCHUP_ERR;
    }
}

int catchup_state_unplace(CatchupState* s) {
    if (!s || s->impl._undo_n <= 0) return CATCHUP_ERR;
    try {
        s->impl.unplace();
        return CATCHUP_OK;
    } catch (...) {
        return CATCHUP_ERR;
    }
}

int catchup_state_end_turn(CatchupState* s) {
    if (!s) return CATCHUP_ERR;
    try {
        s->impl.end_turn();
        return CATCHUP_OK;
    } catch (...) {
        return CATCHUP_ERR;
    }
}

int catchup_state_unend_turn(CatchupState* s) {
    if (!s || s->impl._turn_undo_n <= 0) return CATCHUP_ERR;
    try {
        s->impl.unend_turn();
        return CATCHUP_OK;
    } catch (...) {
        return CATCHUP_ERR;
    }
}

int catchup_state_play_turn(CatchupState* s, const int* cells, int n,
                            char* err, int errlen) {
    if (!s || n < 0 || (n > 0 && !cells)) {
        if (err && errlen > 0) {
            std::strncpy(err, "invalid arguments", static_cast<std::size_t>(errlen - 1));
            err[errlen - 1] = 0;
        }
        return CATCHUP_ERR;
    }
    try {
        s->impl.play_turn(std::vector<int>(cells, cells + n));
        return CATCHUP_OK;
    } catch (const std::exception& e) {
        if (err && errlen > 0) {
            std::strncpy(err, e.what(), static_cast<std::size_t>(errlen - 1));
            err[errlen - 1] = 0;
        }
        return CATCHUP_ERR;
    } catch (...) {
        if (err && errlen > 0) err[0] = 0;
        return CATCHUP_ERR;
    }
}

int catchup_state_undo_turn(CatchupState* s, int stone_count) {
    if (!s || stone_count < 0 || s->impl._turn_undo_n <= 0
        || stone_count > s->impl._undo_n) {
        return CATCHUP_ERR;
    }
    try {
        s->impl.undo_turn(stone_count);
        return CATCHUP_OK;
    } catch (...) {
        return CATCHUP_ERR;
    }
}

int catchup_state_find(const CatchupState* s, int cell) {
    if (!s || cell < 0 || cell >= catchup::NUM_CELLS) return CATCHUP_ERR;
    return s->impl.find(cell);
}

uint64_t catchup_state_largest_lib_mask(const CatchupState* s, int player) {
    return s->impl.largest_lib_mask(player);
}

int catchup_state_largest_liberties(const CatchupState* s, int player) {
    return s->impl.largest_liberties(player);
}

int catchup_state_max_reachable(const CatchupState* s, int player) {
    return s->impl.max_reachable(player);
}

int catchup_state_largest_reachable(const CatchupState* s, int player) {
    return s->impl.largest_reachable(player);
}

int catchup_state_score(const CatchupState* s, int player) {
    return s->impl.score(player);
}

int catchup_state_group_sizes(const CatchupState* s, int player, int* out, int cap) {
    if (!s || player < 0 || player > 1) return CATCHUP_ERR;
    try {
        auto sizes = s->impl.group_sizes(player);
        int n = static_cast<int>(sizes.size());
        if (n > cap) n = cap;
        for (int i = 0; out && i < n; ++i) out[i] = sizes[i];
        return static_cast<int>(sizes.size());
    } catch (...) {
        return CATCHUP_ERR;
    }
}

int catchup_state_compare_final(const CatchupState* s) {
    return s->impl.compare_final();
}

int catchup_state_terminal_score(const CatchupState* s, int player) {
    return s->impl.terminal_score(player);
}

int catchup_state_legal_turn_sizes(const CatchupState* s, int* out, int cap) {
    if (!s) return CATCHUP_ERR;
    try {
        auto sizes = s->impl.legal_turn_sizes();
        int n = static_cast<int>(sizes.size());
        if (n > cap) n = cap;
        for (int i = 0; out && i < n; ++i) out[i] = sizes[i];
        return static_cast<int>(sizes.size());
    } catch (...) {
        return CATCHUP_ERR;
    }
}

int catchup_state_is_over(const CatchupState* s) {
    return s->impl.is_over() ? 1 : 0;
}

int catchup_state_placements_left(const CatchupState* s) {
    return s->impl.placements_left();
}

CatchupClock* catchup_clock_new(double total, double reserve) {
    try {
        auto* c = new CatchupClock();
        c->impl = Clock(total, reserve);
        return c;
    } catch (...) {
        return nullptr;
    }
}

void catchup_clock_free(CatchupClock* c) { delete c; }

double catchup_clock_remaining(const CatchupClock* c) { return c->impl.remaining(); }

double catchup_clock_allocate(const CatchupClock* c, const CatchupState* s) {
    if (!c || !s) return 0.0;
    try {
        return c->impl.allocate(s->impl);
    } catch (...) {
        return 0.0;
    }
}

void catchup_clock_consume(CatchupClock* c, double seconds) { c->impl.consume(seconds); }
void catchup_clock_note_own(CatchupClock* c, int stones) { c->impl.note_own_stones(stones); }
void catchup_clock_note_reply(CatchupClock* c, int stones) { c->impl.note_reply_stones(stones); }
void catchup_clock_undo_note(CatchupClock* c) { c->impl.undo_note(); }
double catchup_clock_get_used(const CatchupClock* c) { return c->impl.used; }
void catchup_clock_set_used(CatchupClock* c, double used) { c->impl.used = used; }
double catchup_clock_get_total(const CatchupClock* c) { return c->impl.total; }
double catchup_clock_get_reserve(const CatchupClock* c) { return c->impl.reserve; }

CatchupEngine* catchup_engine_new(const CatchupEngineConfig* cfg_in) {
    CatchupEngineConfig cfg = cfg_in ? *cfg_in : defaults_cfg();
    // The Engine constructor allocates the transposition table and can throw
    // std::bad_alloc; new (std::nothrow) would not have caught that.
    try {
        return new CatchupEngine(cfg);
    } catch (...) {
        return nullptr;
    }
}

void catchup_engine_free(CatchupEngine* e) { delete e; }

void catchup_engine_new_game(CatchupEngine* e) { e->impl.new_game(); }

void catchup_engine_set_deadline(CatchupEngine* e, double deadline) {
    e->impl.deadline.store(deadline);
}

double catchup_engine_get_deadline(const CatchupEngine* e) {
    return e->impl.deadline.load();
}

int catchup_engine_find_turn(CatchupEngine* e, CatchupState* s, double time_limit,
                             CatchupSearchStats* stats) {
    if (!e || !s) return CATCHUP_ERR;
    std::pair<std::vector<int>, catchup::SearchStats> result;
    try {
        result = e->impl.find_turn(s->impl, time_limit);
    } catch (...) {
        return CATCHUP_ERR;
    }
    if (stats) {
        stats->nodes = result.second.nodes;
        stats->depth = result.second.depth;
        stats->value = result.second.value;
        stats->completed = result.second.completed;
        stats->elapsed = result.second.elapsed;
        int n = static_cast<int>(std::min<std::size_t>(result.first.size(), 3));
        stats->turn_n = n;
        for (int i = 0; i < n; ++i) stats->turn[i] = result.first[i];
    }
    return CATCHUP_OK;
}

void catchup_engine_set_eval_weights(CatchupEngine* e, const int* w15) {
    if (!e || !w15) return;
    e->impl.w_largest = w15[0];
    e->impl.w_sumsq = w15[1];
    e->impl.w_frontier = w15[2];
    e->impl.w_centre = w15[3];
    e->impl.w_tempo = w15[4];
    e->impl.w_pending = w15[5];
    e->impl.w_liberty = w15[6];
    e->impl.w_stones = w15[7];
    e->impl.w_second = w15[8];
    e->impl.w_join = w15[9];
    e->impl.w_crit = w15[10];
    e->impl.w_third = w15[11];
    e->impl.w_fourth = w15[12];
    e->impl.w_gift = w15[13];
    e->impl.w_reachable = w15[14];
}

void catchup_engine_get_eval_weights(const CatchupEngine* e, int* w15) {
    if (!e || !w15) return;
    w15[0] = e->impl.w_largest;
    w15[1] = e->impl.w_sumsq;
    w15[2] = e->impl.w_frontier;
    w15[3] = e->impl.w_centre;
    w15[4] = e->impl.w_tempo;
    w15[5] = e->impl.w_pending;
    w15[6] = e->impl.w_liberty;
    w15[7] = e->impl.w_stones;
    w15[8] = e->impl.w_second;
    w15[9] = e->impl.w_join;
    w15[10] = e->impl.w_crit;
    w15[11] = e->impl.w_third;
    w15[12] = e->impl.w_fourth;
    w15[13] = e->impl.w_gift;
    w15[14] = e->impl.w_reachable;
}

int catchup_engine_evaluate(CatchupEngine* e, CatchupState* s, int placements_left) {
    if (!e || !s) return 0;
    catchup::GameState* saved = e->impl.st;
    e->impl.st = &s->impl;
    int value = 0;
    try {
        value = e->impl._evaluate(placements_left);
    } catch (...) {
        value = 0;
    }
    e->impl.st = saved;  // do not leave the engine pointing at the caller's state
    return value;
}
