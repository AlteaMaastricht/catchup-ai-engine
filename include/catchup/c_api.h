#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CATCHUP_NUM_CELLS = 61,
    CATCHUP_OK = 0,
    CATCHUP_ERR = -1
};

typedef struct CatchupState CatchupState;
typedef struct CatchupEngine CatchupEngine;
typedef struct CatchupClock CatchupClock;

typedef struct CatchupStateData {
    uint8_t colour[61];
    uint64_t bb[2];
    uint8_t adjacency[2][61];
    uint64_t touch[2];
    uint64_t join[2];
    int empty_count;
    int parent[61];
    int size[61];
    int size_n[2][62];
    uint64_t lib_root[61];
    int largest[2];
    int second[2];
    int largest_root[2];
    int sumsq[2];
    int nstones[2];
    int centrality[2];
    int frontier[2];
    int to_move;
    int turn_index;
    int placed_this_turn;
    int max_this_turn;
    int score_at_turn_start;
    int score_increased[2];
    int allows_three;
    uint64_t zobrist;
    int undo_n;
    int turn_undo_n;
    int lib_undo_n;
} CatchupStateData;

typedef struct CatchupBoardTables {
    char cell_names[61][4];
    int cell_x[61];
    int cell_y[61];
    int neighbours[61][6];
    int neighbour_count[61];
    uint64_t neighbour_mask[61];
    int ring[61];
    int centrality[61];
    int static_order[61];
    int static_rank[61];
    uint64_t full_mask;
    uint64_t index_mask_from[62];
    uint64_t zobrist_cell[2][61];
    uint64_t zobrist_side;
    uint64_t zobrist_allows_three;
    uint64_t zobrist_placed[4];
    int centre_cell;
    int display_number[9];
    int display_indent[9];
    int display_ncells[9];
    int display_cells[9][9];
} CatchupBoardTables;

typedef struct CatchupEngineConfig {
    int use_tt, use_pvs, use_killers, use_history, use_aspiration, use_lmr;
    int use_opening_book;
    int max_depth;
    int aspiration_delta;
    int w_largest, w_sumsq, w_frontier, w_centre, w_tempo;
    int w_pending, w_liberty, w_stones, w_second, w_join;
    int w_crit, w_third, w_fourth;
    int use_rfp, use_lmp, use_iid;
    int w_gift;
    int w_reachable;
    int tt_bits;
    int use_countermove;
    int use_stable_stop;
} CatchupEngineConfig;

typedef struct CatchupSearchStats {
    int nodes;
    int depth;
    int value;
    int completed;
    double elapsed;
    int turn[3];
    int turn_n;
} CatchupSearchStats;

/* Error handling: no C++ exception crosses this boundary. Functions that can
 * fail return CATCHUP_ERR (or nullptr / 0.0); the caller decides what to do. */

/* sizeof(CatchupStateData), sizeof(CatchupBoardTables),
 * sizeof(CatchupEngineConfig), sizeof(CatchupSearchStats) - lets bindings that
 * mirror these structs by hand (catchup/_lib.py) detect a layout mismatch. */
void catchup_abi_sizes(int* out4);

void catchup_board_tables(CatchupBoardTables* out);
int catchup_parse_cell(const char* text);

CatchupState* catchup_state_new(void);
void catchup_state_free(CatchupState* s);
CatchupState* catchup_state_clone(const CatchupState* s);
void catchup_state_get(const CatchupState* s, CatchupStateData* out);
void catchup_state_set(CatchupState* s, const CatchupStateData* in);

int catchup_state_place(CatchupState* s, int cell);
int catchup_state_unplace(CatchupState* s);
int catchup_state_end_turn(CatchupState* s);
int catchup_state_unend_turn(CatchupState* s);
int catchup_state_play_turn(CatchupState* s, const int* cells, int n,
                            char* err, int errlen);
int catchup_state_undo_turn(CatchupState* s, int stone_count);
int catchup_state_find(const CatchupState* s, int cell);
uint64_t catchup_state_largest_lib_mask(const CatchupState* s, int player);
int catchup_state_largest_liberties(const CatchupState* s, int player);
int catchup_state_max_reachable(const CatchupState* s, int player);
int catchup_state_largest_reachable(const CatchupState* s, int player);
int catchup_state_score(const CatchupState* s, int player);
int catchup_state_group_sizes(const CatchupState* s, int player, int* out, int cap);
int catchup_state_compare_final(const CatchupState* s);
int catchup_state_terminal_score(const CatchupState* s, int player);
int catchup_state_legal_turn_sizes(const CatchupState* s, int* out, int cap);
int catchup_state_is_over(const CatchupState* s);
int catchup_state_placements_left(const CatchupState* s);

CatchupClock* catchup_clock_new(double total, double reserve);
void catchup_clock_free(CatchupClock* c);
double catchup_clock_remaining(const CatchupClock* c);
double catchup_clock_allocate(const CatchupClock* c, const CatchupState* s);
void catchup_clock_consume(CatchupClock* c, double seconds);
void catchup_clock_note_own(CatchupClock* c, int stones);
void catchup_clock_note_reply(CatchupClock* c, int stones);
void catchup_clock_undo_note(CatchupClock* c);
double catchup_clock_get_used(const CatchupClock* c);
void catchup_clock_set_used(CatchupClock* c, double used);
double catchup_clock_get_total(const CatchupClock* c);
double catchup_clock_get_reserve(const CatchupClock* c);

CatchupEngine* catchup_engine_new(const CatchupEngineConfig* cfg);
void catchup_engine_free(CatchupEngine* e);
void catchup_engine_new_game(CatchupEngine* e);
void catchup_engine_set_deadline(CatchupEngine* e, double deadline);
double catchup_engine_get_deadline(const CatchupEngine* e);
int catchup_engine_find_turn(CatchupEngine* e, CatchupState* s, double time_limit,
                             CatchupSearchStats* stats);
void catchup_engine_set_eval_weights(CatchupEngine* e, const int* w15);
void catchup_engine_get_eval_weights(const CatchupEngine* e, int* w15);
int catchup_engine_evaluate(CatchupEngine* e, CatchupState* s, int placements_left);

#ifdef __cplusplus
}
#endif
