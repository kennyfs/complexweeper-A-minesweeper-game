#!/usr/bin/env python3
"""Headless self-test for the GUI support code (no window needed).

    python3 gui/selftest.py [--snapshots DIR]

--snapshots writes a few rendered boards as PNG files so the artwork mapping can be checked by eye.
"""
import argparse
import itertools
import random
import sys
from pathlib import Path

import engine as E
from sprites import Atlas, cell_sprite, number_name, render_board

failures = 0
checks = 0


def expect(cond, text):
    global failures, checks
    checks += 1
    if not cond:
        failures += 1
        print("  [FAIL]", text)


def displayed_values(hyper):
    """Every number a cell can show: enumerate all neighborhoods of up to 8 mines."""
    values = set()
    for n1, n2, n3, n4 in itertools.product(range(9), repeat=4):
        if n1 + n2 + n3 + n4 <= 8:
            a, b = n1 - n2, n3 - n4
            values.add(a * a - b * b if hyper else a * a + b * b)
    return sorted(values)


def test_sprite_coverage(atlas):
    for hyper in (False, True):
        values = displayed_values(hyper)
        expect(len(values) == (39 if hyper else 24), "number of displayed values (%s mode)" % ("Minkowski" if hyper else "complex"))
        names = [number_name(v, hyper, atlas) for v in values]
        expect(all(atlas.has(n) for n in names), "every displayed value has a sprite (hyper=%s)" % hyper)
        expect(len(set(names)) == len(names), "no two displayed values share a sprite (hyper=%s)" % hyper)
    for name in ["closed", "blank", "face_normal", "face_dead", "face_win", "face_scan", "led_minus", "solver_flag"] + \
            ["led_%d" % d for d in range(10)]:
        expect(atlas.has(name), "sprite exists: " + name)
    for hyper in (False, True):
        for t in range(1, 5):
            for prefix in ("flag", "mine", "boom", "wrong", "rightflag", "wrongflag"):
                name = ("h" if hyper and t >= 3 else "") + "%s_%d" % (prefix, t)
                expect(atlas.has(name), "sprite exists: " + name)
    print("sprite coverage: done")


def play(eng, atlas, snapshots, label):
    """Play one game with the solver, checking sprites, hidden mines and undo on the way."""
    hyper = eng.mode == E.MODE_HYPER
    n = eng.width * eng.height
    leaked = False
    bad_sprite = []
    steps = 0
    snapped_mid = False
    while eng.state not in (E.WON, E.LOST):
        mv = eng.solver_step(0)
        if mv is None:
            break
        steps += 1
        cells = eng.cells()
        over = eng.state in (E.WON, E.LOST)
        if not over:
            leaked = leaked or any(c.mine for c in cells)
        for i, c in enumerate(cells):
            name = cell_sprite(c, i, hyper, over, eng.boom_cell, atlas)
            if not atlas.has(name):
                bad_sprite.append(name)
        if snapshots and not snapped_mid and steps == 12:
            render_board(cells, eng.width, eng.height, hyper, False, -1, atlas).save(snapshots / ("%s_mid.png" % label))
            snapped_mid = True
    cells = eng.cells()
    if snapshots:
        render_board(cells, eng.width, eng.height, hyper, True, eng.boom_cell, atlas).save(snapshots / ("%s_end.png" % label))
    expect(not leaked, "%s: mines are never exposed while the game is running" % label)
    expect(not bad_sprite, "%s: every cell maps to an existing sprite %s" % (label, bad_sprite[:3]))
    expect(eng.state in (E.WON, E.LOST), "%s: the solver plays the game to its end" % label)
    expect(any(c.mine for c in cells), "%s: mines are revealed after the game" % label)
    # Undo everything and check we land back at the very start.
    depth = eng.undo_depth
    expect(depth == steps, "%s: one history entry per step" % label)
    while eng.undo():
        pass
    cells = eng.cells()
    expect(eng.state == E.READY and eng.undo_depth == 0, "%s: undo restores the starting state" % label)
    expect(not any(c.open or c.flag or c.mark for c in cells), "%s: no open / flagged / marked cell remains" % label)
    return n


def test_games(atlas, snapshots):
    eng = E.Engine()
    rng = random.Random(7)
    for mode, mode_name in ((E.MODE_COMPLEX, "complex"), (E.MODE_HYPER, "mink")):
        for level, (w, h, mines) in E.LEVELS.items():
            for k in range(3):
                eng.new_game(w, h, mines, mode, rng.randrange(1, 10 ** 6))
                expect((eng.width, eng.height, eng.mines, eng.mode) == (w, h, mines, mode), "new_game applies its parameters")
                play(eng, atlas, snapshots if k == 0 else None, "%s_%s" % (mode_name, level.lower()))
    print("games with solver and undo: done")


def test_manual(atlas):
    eng = E.Engine()
    eng.new_game(9, 9, 10, E.MODE_COMPLEX, 3)
    expect(not eng.cycle_flag(0), "cannot flag before the game starts")
    expect(eng.click(40, 0) and eng.state == E.PLAYING, "first click starts the game")
    expect(not eng.click(-1) and not eng.click(999), "out-of-range clicks are ignored")
    closed = next(i for i, c in enumerate(eng.cells()) if not c.open)
    expect(eng.cycle_flag(closed) and eng.cells()[closed].flag == 1, "right click places a +1 flag")
    expect(not eng.click(closed), "a flagged cell cannot be opened")
    expect(eng.undo() and eng.cells()[closed].flag == 0, "undo removes the flag")
    print("manual actions: done")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--snapshots", metavar="DIR", help="write rendered board images here")
    args = parser.parse_args()
    snapshots = Path(args.snapshots) if args.snapshots else None
    if snapshots:
        snapshots.mkdir(parents=True, exist_ok=True)
    atlas = Atlas()
    test_sprite_coverage(atlas)
    test_manual(atlas)
    test_games(atlas, snapshots)
    print("\n%d checks, %d failed\n%s" % (checks, failures, "ALL PASSED" if not failures else "FAILURES"))
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
