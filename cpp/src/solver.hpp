// Complexweeper solver.
//
// It only uses what a human player can see: which cells are open, the number shown on each
// open cell, and whether an open cell is "blank" (no mines around at all, drawn differently
// from a cell showing 0). It never reads mine[].
//
// Model: every closed cell is a variable with value 0 (no mine) or 1..4 (the four mine types).
// Every open cell that still has closed neighbors is a constraint: the displayed value of the
// summed mine vector of its neighborhood must equal the number shown.
//
// Strategy, cheapest first:
//   1. Constraint propagation (enumerate each constraint alone, drop values with no support)
//      until a fixpoint -> cells that are certainly safe / certainly mines.
//   2. Exhaustive enumeration of each connected component (with a node budget). If a component
//      is too large, enumerate local windows around each constraint instead; a conclusion that
//      holds for a subset of the constraints also holds for all of them.
//   3. If nothing is certain, guess the closed cell with the lowest estimated mine probability.
//
// Mine types are never deducible from the numbers (negating every mine, or swapping real and
// imaginary in the complex mode, leaves every number unchanged), so the solver only decides
// "mine or not" and does not try to tell which type a mine is.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game.hpp"

namespace cw {

enum class MoveKind : std::uint8_t { none = 0, open, mark };
enum class Reason : std::uint8_t {
    first_click,  // the game has not started: click the center (it and its neighbors are safe)
    certain,      // logically certain
    guess,        // a guess; risk is the estimated probability that the cell is a mine
};

struct Move {
    MoveKind kind = MoveKind::none;
    std::uint16_t cell = 0;
    Reason reason = Reason::certain;
    float risk = 0.0f;  // 0..1, only meaningful for guesses
};

class Solver {
public:
    // Advance one step and apply it to `g`. Returns what was done; kind == none means there is
    // nothing left to do (the game is over, or every remaining closed cell is already marked).
    Move step(Game& g, std::uint32_t now_ms = 0);

    // Cells the solver has marked as mines.
    bool isMarked(std::size_t cell) const { return cell < marks_.size() && marks_[cell] != 0; }
    std::size_t markCount() const;

private:
    // Analyze the board and put the next steps in queue_ (certain steps, or a single guess).
    // Returns false if there is no cell left to play.
    bool analyze(const Game& g);

    std::array<std::uint8_t, MAX_CELLS> marks_{};
    std::vector<Move> queue_;  // stored in reverse order; the next step is at the back
};

}  // namespace cw
