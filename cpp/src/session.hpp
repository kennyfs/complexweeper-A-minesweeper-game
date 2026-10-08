// Complexweeper session: game state + solver + undo history.
//
// Before every action that changes the position (a manual reveal / flag / chord, or one solver
// step) the whole state is saved as a snapshot; undo() simply restores the latest snapshot.
// Actions that do not actually change anything leave no history entry.
#pragma once

#include <cstddef>
#include <deque>

#include "game.hpp"
#include "solver.hpp"

namespace cw {

class Session {
public:
    explicit Session(std::size_t history_limit = 2000) : limit_(history_limit) {}

    Game game;
    Solver solver;

    // Start a new game and clear the history and the solver state. The solver orientation is kept.
    void newGame(std::uint16_t w, std::uint16_t h, std::uint16_t mines, Mode mode, std::uint32_t seed);
    // Which of the equivalent flag labelings the solver produces (see Solver::setOrientation).
    void setSolverOrientation(int orientation) {
        orientation_ = orientation & 7;
        solver.setOrientation(orientation_);
    }

    // Manual actions, same meaning as the Game functions of the same name.
    // They return true if the position actually changed.
    bool click(std::size_t cell, std::uint32_t now_ms);  // starts the game if needed, else reveals
    bool cycleFlag(std::size_t cell);
    bool chord(std::size_t cell);

    // One solver step (open one cell or mark one mine). kind == none means nothing to do.
    Move solverStep(std::uint32_t now_ms);

    bool undo();
    std::size_t undoDepth() const { return history_.size(); }

    // Number of flagged cells (for the "mines left" display).
    std::size_t markedCount() const;

private:
    struct Snapshot {
        Game game;
        Solver solver;
    };
    void checkpoint();
    void dropCheckpoint() { history_.pop_back(); }
    // Runs `action`, keeping a history entry only if it changed the position.
    template <class F>
    bool recorded(F&& action);
    static bool sameState(const Game& a, const Game& b);

    std::deque<Snapshot> history_;
    std::size_t limit_;
    int orientation_ = 0;
};

}  // namespace cw
