// Complexweeper solver.
//
// It only uses what a human player can see: which cells are open, the number shown on each
// open cell, whether an open cell is "blank" (no mines around at all, drawn differently from a
// cell showing 0), and the total number of mines. It never reads mine[].
//
// Model: every closed cell is a variable with value 0 (no mine) or 1..4 (the four mine types).
// Every open cell that still has closed neighbors is a constraint: the displayed value of the
// summed mine vector of its neighborhood must equal the number shown.
//
// Finding the mines, cheapest first:
//   1. Constraint propagation (enumerate each constraint alone, drop values with no support)
//      until a fixpoint -> cells that are certainly safe / certainly mines.
//   2. The total mine count: all closed cells are mines (or all are safe) when it says so.
//   3. Exhaustive enumeration of each connected component (with a node budget). If a component
//      is too large, enumerate local windows around each constraint instead; a conclusion that
//      holds for a subset of the constraints also holds for all of them.
//   4. If nothing is certain, guess the closed cell with the lowest estimated mine probability.
//
// Flags. The solver places real typed flags on the game. The numbers can never tell the absolute
// type of a mine: every connected component of mines can be negated, have its real and imaginary
// parts negated separately and (in the complex mode) have the two parts swapped without changing
// a single number (8 symmetries, 4 in the Minkowski mode where swapping is not allowed). The
// rules accept any labeling that reproduces all numbers, so the solver just picks one:
//   * a new flag gets the first type, in the order given by the orientation, that still lets
//     every open number be satisfied (so the first mine of a component is +1 and the first
//     mine on the other axis is +i / +j, in the canonical orientation);
//   * when a newly opened number shows that two groups of flags were oriented inconsistently,
//     the flags of the affected components are re-oriented.
// The orientation only changes which of the equivalent labelings is chosen, so that repeated
// runs do not all look the same.
#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "game.hpp"

namespace cw {

enum class MoveKind : std::uint8_t {
    none = 0,
    open,
    mark,    // place a typed flag on a mine
    retype,  // no cell changed, but flags were re-oriented so that all numbers agree
};
enum class Reason : std::uint8_t {
    first_click,  // the game has not started: click the center (it and its neighbors are safe)
    certain,      // logically certain
    guess,        // a guess; risk is the estimated probability that the cell is a mine
};

struct Move {
    MoveKind kind = MoveKind::none;
    std::uint16_t cell = 0;
    Reason reason = Reason::certain;
    std::uint8_t type = 0;       // mark: the type of the flag that was placed (1..4)
    std::uint16_t retyped = 0;   // number of other flags whose type had to change in this step
    float risk = 0.0f;           // 0..1, only meaningful for guesses
};

class Solver {
public:
    // Advance one step and apply it to `g`. Returns what was done; kind == none means there is
    // nothing left to do (the game is over, or the solver is stuck).
    Move step(Game& g, std::uint32_t now_ms = 0);

    // Which of the equivalent flag labelings to produce: 0 is the canonical one (first mine is
    // +1, first mine on the other axis is +i / +j). 0..7 are the 8 symmetries (0..3 are used in
    // the Minkowski mode).
    void setOrientation(int orientation) { orientation_ = orientation & 7; }
    int orientation() const { return orientation_; }

    // Cells the solver has flagged as certain mines.
    bool isMarked(std::size_t cell) const { return cell < marks_.size() && marks_[cell] != 0; }
    std::size_t markCount() const;

private:
    bool analyze(const Game& g);
    // Drop marks whose flag the player removed.
    void syncMarks(const Game& g);
    // The flag type for a new mine at `cell` (see the class comment).
    std::uint8_t chooseType(const Game& g, std::size_t cell) const;
    // Re-orient flags if the numbers contradict their current types. Returns how many flags
    // changed type.
    std::size_t repair(Game& g, std::uint64_t budget);

    std::array<std::uint8_t, MAX_CELLS> marks_{};
    std::vector<Move> queue_;  // stored in reverse order; the next step is at the back
    int orientation_ = 0;
};

}  // namespace cw
