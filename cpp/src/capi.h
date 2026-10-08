/* Complexweeper C interface (for Python ctypes and other foreign languages).
 *
 * Cell index = row * width + column, starting at 0.
 * mode: 0 = circular complex mode, 1 = Minkowski (hyperbolic) mode. */
#ifndef CW_CAPI_H
#define CW_CAPI_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct cw_session cw_session;

/* What is visible about one cell. `mine` is only filled once the game is over (1..4); it is
 * always 0 while the game is in progress. */
typedef struct {
    int16_t clue;     /* displayed value of an open cell; -1 for a closed cell */
    uint8_t open;     /* 1 = open */
    uint8_t flag;     /* the player's flag: 0 none, 1..4 flag type */
    uint8_t mine;     /* only after the game is over: 0 no mine, 1..4 mine type */
    uint8_t mark;     /* 1 = the solver has marked this cell as a mine */
    uint8_t blank;    /* open and no mines around at all (different from a displayed 0) */
    uint8_t reserved;
} cw_cell;

/* One solver step. kind: 0 nothing to do, 1 open, 2 mark as mine.
 * reason: 0 first click, 1 logically certain, 2 guess (risk = mine probability). */
typedef struct {
    int32_t kind;
    int32_t cell;
    int32_t reason;
    float risk;
} cw_move;

cw_session* cw_create(void);
void cw_destroy(cw_session* s);

/* Start a new game. w, h and mines are clamped to legal ranges; read the real values back with
 * cw_width() etc. */
void cw_new_game(cw_session* s, int w, int h, int mines, int mode, uint32_t seed);
void cw_set_judge_loose(cw_session* s, int loose);

int cw_width(const cw_session* s);
int cw_height(const cw_session* s);
int cw_mines(const cw_session* s);
int cw_mode(const cw_session* s);
/* 0 not started, 1 playing, 2 won, 3 lost */
int cw_state(const cw_session* s);
/* The cell that was stepped on, or -1 */
int cw_boom_cell(const cw_session* s);
/* Number of cells with a player flag or a solver mark */
int cw_marked_count(const cw_session* s);
/* Number of steps that can be undone */
int cw_undo_depth(const cw_session* s);

/* Writes all width*height cells; `out` must have room for that many elements. */
void cw_get_cells(const cw_session* s, cw_cell* out);

/* Manual actions. Return 1 if the position changed, 0 if the action had no effect. */
int cw_click(cw_session* s, int cell, uint32_t now_ms);
int cw_cycle_flag(cw_session* s, int cell);
int cw_chord(cw_session* s, int cell);

/* Performs one solver step and stores it in *out. Returns 1 if a step was made, 0 if there was
 * nothing to do. */
int cw_solver_step(cw_session* s, uint32_t now_ms, cw_move* out);
/* Undoes the latest step (manual or solver). Returns 1 if something was undone. */
int cw_undo(cw_session* s);

#ifdef __cplusplus
}
#endif

#endif
