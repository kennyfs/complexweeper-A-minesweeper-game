// Board generator: turns a random mine layout into one that logic alone can solve.
//
// The layout is checked with the solver in its no-guess mode (Solver::solveByLogic). While the
// solver gets stuck, a few mines near the place where it got stuck are moved or changed, and the
// board is checked again; changes are kept when the number of safe cells left unsolved does not
// grow. Moving a mine off the border of the stuck region into the unknown interior lowers the
// local density, which is what usually unsticks the solver.
//
// Everything is driven by Game::rng, so the same seed and first click give the same board.
#pragma once

#include <cstddef>

#include "game.hpp"

namespace cw {

// Rearranges g.mine (already laid out by Game::layMines) until the solver needs no guess from
// `start`. After too many failed attempts it starts over with a fresh random layout, and after
// several restarts it gives up and keeps the last layout (this only happens on boards so dense
// that no solvable layout exists). The clues are not touched.
void makeSolvable(Game& g, std::size_t start);

}  // namespace cw
