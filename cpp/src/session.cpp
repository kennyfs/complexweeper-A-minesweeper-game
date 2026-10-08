#include "session.hpp"

#include <algorithm>

namespace cw {

void Session::newGame(std::uint16_t w, std::uint16_t h, std::uint16_t mines, Mode mode, std::uint32_t seed) {
    game.w = w;
    game.h = h;
    game.mines = mines;
    game.mode = mode;
    game.newGame(seed);
    solver = Solver{};
    history_.clear();
}

void Session::checkpoint() {
    history_.push_back({game, solver});
    if (history_.size() > limit_) history_.pop_front();
}

// Compares the parts the player can see and that actions can change.
bool Session::sameState(const Game& a, const Game& b) {
    return a.n == b.n && a.started == b.started && a.over == b.over && a.win == b.win && a.moves == b.moves &&
           std::equal(a.open.begin(), a.open.begin() + static_cast<std::ptrdiff_t>(a.n), b.open.begin()) &&
           std::equal(a.flag.begin(), a.flag.begin() + static_cast<std::ptrdiff_t>(a.n), b.flag.begin());
}

template <class F>
bool Session::recorded(F&& action) {
    checkpoint();
    action();
    if (sameState(history_.back().game, game)) {
        dropCheckpoint();
        return false;
    }
    return true;
}

bool Session::click(std::size_t cell, std::uint32_t now_ms) {
    if (game.over || cell >= game.n) return false;
    return recorded([&] {
        if (!game.started) {
            game.startAt(cell, now_ms);
            game.setMsg(Msg::started);
        } else {
            game.reveal(cell);
        }
    });
}

bool Session::cycleFlag(std::size_t cell) {
    if (!game.started || game.over || cell >= game.n) return false;
    return recorded([&] { game.cycleFlag(cell); });
}

bool Session::chord(std::size_t cell) {
    if (!game.started || game.over || cell >= game.n) return false;
    return recorded([&] { game.tryExpand(cell); });
}

Move Session::solverStep(std::uint32_t now_ms) {
    if (game.over) return {};
    checkpoint();
    const Move m = solver.step(game, now_ms);
    if (m.kind == MoveKind::none) dropCheckpoint();
    return m;
}

bool Session::undo() {
    if (history_.empty()) return false;
    game = history_.back().game;
    solver = history_.back().solver;
    history_.pop_back();
    return true;
}

std::size_t Session::markedCount() const {
    std::size_t k = 0;
    for (std::size_t i = 0; i < game.n; ++i) k += (game.flag[i] != 0 || solver.isMarked(i));
    return k;
}

}  // namespace cw
