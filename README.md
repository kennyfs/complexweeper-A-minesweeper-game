# Complexweeper

Minesweeper where the mines are complex numbers. The number on a cell is the modulus of the
sum of all the mines around it.

There are two modes: the **circular complex** mode (`i² = −1`) and the **Minkowski** mode
(`j² = +1`). The repository contains the game logic in C++20, a solver that plays one step at a
time with undo, and a small tkinter GUI that lets you watch the solver think.

Developed and tested on Linux.

## The game

### Mines, numbers and flags

Every mine is one of four types, a unit vector on the real axis or on the imaginary axis:

| type | circular complex mode | Minkowski mode |
| --- | --- | --- |
| 1 | +1 | +1 |
| 2 | −1 | −1 |
| 3 | +i | +j |
| 4 | −i | −j |

An open cell shows the modulus of the sum of the mines in its eight neighbors. Right click cycles
a flag on a closed cell: none → +1 → −1 → +i (+j) → −i (−j) → none.

**Circular complex mode.** With `a` the real part and `b` the imaginary part of the sum, the cell
shows `√(a² + b²)`, written as an integer or a simplified radical. Because a cell has at most 8
neighbors only 24 numbers can occur:

> 0, 1, 2, 3, 4, 5, 6, 7, 8
> √2, √5, √10, √13, √17, √26, √29, √34, √37
> 2√2, 2√5, 3√2, 4√2, 5√2, 2√10

For example three mines +1, +1 and +i around a cell sum to 2 + i and show √5. A +1 and a −1 (or a
+i and a −i) cancel each other; such a pair is called a *cancelling pair*. A **blank** cell has no
mine around it at all, while a **0** means the mines around it cancel out completely. They look
different and are not the same thing.

**Minkowski mode.** The hyperbolic unit `j` has `j² = +1`, and the formal modulus (the
"spacetime interval") is `√(a² − b²)`, so it can be imaginary: a single +j mine shows `i`. The
mode is only inspired by the Minkowski metric and has nothing to do with general relativity. The 39
numbers that can occur are:

> 0, 1, √3, 2, √5, √7, 2√2, 3, 2√3, √15, 4, √21, 2√6, 5, 4√2, √35, 6, 4√3, 7, 8
> i, √3 i, 2i, √5 i, √7 i, 2√2 i, 3i, 2√3 i, √15 i, 4i, √21 i, 2√6 i, 5i, 4√2 i, √35 i, 6i,
> 4√3 i, 7i, 8i

### Winning and losing

You **lose** by opening a mine. You **win** when both of these hold:

1. the flags sit exactly on the mines (no flag on a safe cell, no mine without a flag), and
2. the flags around every open number add up, in the rules of the current mode, to that number.

Opening every safe cell is not enough: the mines have to be flagged as well.

Flag types do not have to match how the board was generated; any labeling that reproduces all the
numbers wins. That is deliberate, because the numbers can never tell the absolute type of a mine:
every connected group of mines can be negated, have its real and imaginary parts negated
separately and (in the circular complex mode) have the two parts swapped without changing a
single number. That makes 8 equivalent labelings per group in the circular complex mode and 4 in
the Minkowski mode, where swapping is not a symmetry because `a² − b²` changes sign.

When the game is over, the mines are shown with their types, correct flags get a check mark and
flags on cells without a mine get a red cross.

### Chording

Middle click (or Shift + left click) on an open number opens all its unflagged neighbors if the
flags match the real mines around it, and does nothing otherwise:

* **Circular complex mode:** the number of flags equals the number of mines, and the split into
  real-axis and imaginary-axis flags equals the true split or its swap.
* **Minkowski mode:** the number of flags equals the number of mines, and the real part and the
  `j` part of the flags' sum each equal the true ones up to sign.

The check compares against the real mines, so a failed chord is information; it can be used to
probe for cancelling pairs. If the criterion passes but a mine is not covered by a flag, you step
on it.

## What is in this repository

| path | contents |
| --- | --- |
| `cpp/src/game.*` | the rules and the game state (no GUI, no third-party libraries) |
| `cpp/src/solver.*` | the solver |
| `cpp/src/session.*` | game + solver + undo history |
| `cpp/src/capi.*` | a C interface, built as a shared library for the GUI |
| `cpp/tests/` | the rule tests and the solver tests |
| `cpp/tools/winrate.cpp` | multithreaded win-rate measurement of the solver |
| `gui/` | the tkinter GUI (Python, talks to the C++ library through `ctypes`) |
| `tools/tune_mines.py` | tunes the number of mines for a target win rate |
| `素材/` | the sprite atlas used by the GUI (see the asset notice below) |

## Getting started

You need CMake 3.20 or newer and a C++20 compiler (GCC 15 was used). The GUI also needs Python 3
with tkinter and [Pillow](https://pypi.org/project/pillow/).

### Run the GUI

```bash
python3 gui/app.py
```

The first run builds the C++ library with CMake. In the window:

* Left click opens, right click cycles the flag, middle click or Shift + left click chords.
* **Step ▶** lets the solver make one move, **◀ Undo** takes back the last move (the solver's or
  your own), **Auto play** keeps stepping with an adjustable delay.
* Each solver step highlights the cell it acted on and explains why: certainly safe, certainly a
  mine, or a guess with its estimated risk.
* **Random flag orientation** makes the solver pick one of the equivalent flag labelings per
  game, so that solutions do not all look the same. It follows from the seed, so replaying a seed
  looks the same.
* Keys: Right = step, Left = undo, Space = auto play, F2 = new game.
* The window never changes size; the zoom is limited to what fits.

### Run the tests

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build
ctest --test-dir cpp/build
python3 gui/selftest.py
python3 gui/app.py --smoke
```

`ctest` runs the rule tests and the solver tests. `gui/selftest.py` needs no window. `--smoke`
drives the real window with mouse events and briefly opens it, so it needs a display.

## The solver

The solver plays like a careful human: it only uses what is visible (which cells are open, their
numbers, whether a cell is blank, and the total number of mines) and never reads the hidden board.
It is built to be watched, so it makes exactly one move at a time: it either opens one cell (with
its cascade) or places one flag.

Each closed cell is a variable (no mine, or one of four mine types) and each open number is a
constraint on its neighbors. To decide the next move it tries, from cheap to expensive:

1. **Constraint propagation:** enumerate each constraint on its own and drop the values nothing
   supports.
2. **The total mine count:** if no mine is left, every closed cell is safe; if as many mines are
   left as closed cells, all of them are mines.
3. **Exhaustive enumeration** of each connected group of constraints, with a node budget and
   symmetry breaking. A group that is too big is searched in small windows around each constraint
   instead; what holds for some of the constraints also holds for all of them.
4. **A guess**, if nothing is certain: the closed cell with the lowest estimated probability of
   being a mine.

The flags it places are real, typed flags. A new flag gets the first type that still lets every
open number be satisfied, and when a newly opened number shows that two groups of flags were
oriented inconsistently, the affected flags are re-oriented in the same step. Because the rules
accept any consistent labeling, a game that the solver survives ends in a win.

### Difficulty presets

The mine counts were tuned with `tools/tune_mines.py` so that the solver wins about 90% of its
games on Beginner, 80% on Intermediate and 70% on Expert. A human wins less often than the
solver, because the solver guesses at the lowest possible risk and always finds a consistent flag
labeling. Win rates measured with 2000 to 4000 games per entry:

| mode | Beginner (9×9) | Intermediate (16×16) | Expert (30×16) |
| --- | --- | --- | --- |
| circular complex | 7 mines, 92.0% | 24 mines, 81.8% | 47 mines, 69.0% |
| Minkowski | 8 mines, 87.5% | 25 mines, 79.0% | 47 mines, 70.1% |

On the 81-cell Beginner board one mine more or less moves the win rate by about 5 points, so no
count hits 90% exactly; these are the closest ones. The presets live in `cpp/src/game.hpp`.

To tune them again (for example after changing the solver), run:

```bash
python3 tools/tune_mines.py --total-threads 12 --json tuned.json --log tuning_log.jsonl
```

All configurations share one hard budget of worker threads. Every evaluation is appended to the
log, so a partial run is not lost. Use `--targets` to choose other target win rates.

## Assets, credits and license

The code is licensed under GPL-3.0 (see `LICENSE`).

The graphics come in two kinds with different rights; read the asset notice (`素材说明.md`, in
Chinese) before redistributing or using them commercially.

* The original Minesweeper graphics (buttons, flags, mines and their variants) belong to
  Microsoft; the original game was written by Robert Donner and Curt Johnson. They are **not**
  covered by the GPL-3.0 license of this repository.
* The new graphics (the number sprites, the four flags, the LED digits with the `i` and `j`
  units, the faces) and the program icon were drawn by Qingyuexiao (青月晓) and are released under
  GPL-3.0 together with the code.

This program is an independent reimplementation. It is not affiliated with, authorized by or
endorsed by Microsoft. "Minesweeper" and related trademarks belong to their respective owners.
