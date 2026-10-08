#include "capi.h"

#include <new>

#include "session.hpp"

using cw::Game;
using cw::Session;

struct cw_session {
    Session s;
};

namespace {

bool validCell(const cw_session* s, int cell) {
    return cell >= 0 && static_cast<std::size_t>(cell) < s->s.game.n;
}

}  // namespace

extern "C" {

cw_session* cw_create(void) {
    return new (std::nothrow) cw_session{};
}

void cw_destroy(cw_session* s) {
    delete s;
}

void cw_new_game(cw_session* s, int w, int h, int mines, int mode, uint32_t seed) {
    const auto clamp16 = [](int v) { return static_cast<std::uint16_t>(v < 0 ? 0 : (v > 65535 ? 65535 : v)); };
    s->s.newGame(clamp16(w), clamp16(h), clamp16(mines), mode == 1 ? cw::Mode::hyper : cw::Mode::complex, seed);
}

void cw_set_judge_loose(cw_session* s, int loose) {
    s->s.game.judge_loose = loose != 0;
}

int cw_width(const cw_session* s) { return s->s.game.w; }
int cw_height(const cw_session* s) { return s->s.game.h; }
int cw_mines(const cw_session* s) { return s->s.game.mines; }
int cw_mode(const cw_session* s) { return s->s.game.mode == cw::Mode::hyper ? 1 : 0; }

int cw_state(const cw_session* s) {
    const Game& g = s->s.game;
    if (!g.started) return 0;
    if (!g.over) return 1;
    return g.win ? 2 : 3;
}

int cw_boom_cell(const cw_session* s) { return s->s.game.boom; }
int cw_marked_count(const cw_session* s) { return static_cast<int>(s->s.markedCount()); }
int cw_undo_depth(const cw_session* s) { return static_cast<int>(s->s.undoDepth()); }

void cw_get_cells(const cw_session* s, cw_cell* out) {
    const Game& g = s->s.game;
    for (std::size_t i = 0; i < g.n; ++i) {
        const bool opened = g.open[i] != 0;
        out[i].clue = opened ? g.clue[i] : static_cast<int16_t>(-1);
        out[i].open = g.open[i];
        out[i].flag = g.flag[i];
        out[i].mine = g.over ? g.mine[i] : 0;  // never leak mines while the game is running
        out[i].mark = s->s.solver.isMarked(i) ? 1 : 0;
        out[i].blank = opened && g.isBlank(i) ? 1 : 0;
        out[i].reserved = 0;
    }
}

int cw_click(cw_session* s, int cell, uint32_t now_ms) {
    return validCell(s, cell) && s->s.click(static_cast<std::size_t>(cell), now_ms) ? 1 : 0;
}

int cw_cycle_flag(cw_session* s, int cell) {
    return validCell(s, cell) && s->s.cycleFlag(static_cast<std::size_t>(cell)) ? 1 : 0;
}

int cw_chord(cw_session* s, int cell) {
    return validCell(s, cell) && s->s.chord(static_cast<std::size_t>(cell)) ? 1 : 0;
}

int cw_solver_step(cw_session* s, uint32_t now_ms, cw_move* out) {
    const cw::Move m = s->s.solverStep(now_ms);
    if (out != nullptr) {
        out->kind = static_cast<int32_t>(m.kind);
        out->cell = m.cell;
        out->reason = static_cast<int32_t>(m.reason);
        out->risk = m.risk;
    }
    return m.kind != cw::MoveKind::none ? 1 : 0;
}

int cw_undo(cw_session* s) {
    return s->s.undo() ? 1 : 0;
}

}  // extern "C"
