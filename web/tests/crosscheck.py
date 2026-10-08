#!/usr/bin/env python3
"""Checks that web/engine.js behaves exactly like the C++ library: same boards, same solver moves.

Run from the repository root:   python3 web/tests/crosscheck.py [games-per-setting]
Needs node and the C++ library (built on demand by gui/engine.py).
"""
import json
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent.parent / "gui"))
import engine as eng_mod  # noqa: E402
from engine import Engine, LEVEL_NAMES  # noqa: E402


def python_trace(cfg):
    e = Engine()
    w, h, m = e.preset(cfg["mode"], LEVEL_NAMES[cfg["level"]])
    e.set_solver_orientation(cfg["orient"])
    e.new_game(w, h, m, cfg["mode"], cfg["seed"])
    moves = []
    for k in range(600):
        mv = e.solver_step(0)
        if mv is None:
            break
        moves.append([mv.kind, mv.cell, mv.reason, mv.type, mv.retyped, round(mv.risk, 4)])
        if k == 20:
            for _ in range(5):
                e.undo()
            for _ in range(5):
                e.solver_step(0)
    cells = [[c.clue, c.open, c.flag, c.mine, c.blank] for c in e.cells()]
    return {"moves": moves, "state": e.state, "depth": e.undo_depth, "cells": cells}


def main():
    per = int(sys.argv[1]) if len(sys.argv) > 1 else 6
    configs = [{"mode": mode, "level": level, "seed": 1000 * level + 7 * i + 1 + mode, "orient": (i * 3 + mode) % 8}
               for mode in (0, 1) for level in (0, 1, 2) for i in range(per)]
    out = subprocess.run(["node", str(HERE / "trace.js"), json.dumps(configs)], check=True,
                         capture_output=True, text=True).stdout
    js = json.loads(out)
    bad = 0
    for cfg, got in zip(configs, js):
        want = python_trace(cfg)
        for key in ("moves", "state", "depth", "cells"):
            if got[key] != want[key]:
                bad += 1
                where = ""
                if key == "moves":
                    n = next((i for i, (a, b) in enumerate(zip(got[key], want[key])) if a != b), min(len(got[key]), len(want[key])))
                    where = " first difference at move %d: js %s, c++ %s" % (n, got[key][n:n + 1], want[key][n:n + 1])
                print("MISMATCH", cfg, key, where)
                break
    wins = sum(g["state"] == 2 for g in js)
    print("%d games, %d mismatches, js won %d" % (len(configs), bad, wins))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
