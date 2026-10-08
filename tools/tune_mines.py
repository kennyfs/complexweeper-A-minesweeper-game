#!/usr/bin/env python3
"""Tune the number of mines so that the solver wins a chosen share of its games.

For every (mode, board size) it searches for a mine count whose 95% Wilson confidence interval
for the solver's win rate contains the target rate of that difficulty (default: Beginner 90%,
Intermediate 80%, Expert 70%):

  1. Coarse bisection with small samples (the win rate falls as mines are added).
  2. Verification with fresh seeds and a big sample: starting from the coarse answer, walk one
     mine at a time towards the mine count whose rate is closest to the target until a CI
     contains it. If the walk gets bracketed (one mine count is above the target, its neighbor
     below, and neither CI contains it), repeat with half the sample size: a smaller sample gives
     a wider interval, so eventually a neighbor qualifies. If none ever does, the evaluated mine
     count closest to the target is used and flagged.

All configurations run concurrently and share one hard budget of worker threads (--total-threads,
default: all CPUs). A job leases threads for the duration of one `winrate` run and never takes more
than its share, so the total number of busy threads never exceeds the budget. A job's share is
proportional to the work that is left (board cells), so threads freed by finished jobs flow to the
remaining ones.

    cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release && cmake --build cpp/build --target winrate
    python3 tools/tune_mines.py --total-threads 12 --json tuned.json --log tuning_log.jsonl

Every single evaluation is appended to the --log file as one JSON line, so partial results survive
an interrupted run.
"""
import argparse
import contextlib
import json
import math
import os
import subprocess
import sys
import threading
import time
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
Z = 1.96  # 95%

SIZES = [("Beginner", 9, 9), ("Intermediate", 16, 16), ("Expert", 30, 16)]
DEFAULT_TARGETS = "0.9,0.8,0.7"  # Beginner, Intermediate, Expert
MODES = [(0, "complex"), (1, "minkowski")]

print_lock = threading.Lock()


def log(label, text):
    with print_lock:
        print("[%s] [%-26s] %s" % (time.strftime("%H:%M:%S"), label, text), file=sys.stderr, flush=True)


def wilson(wins, n):
    """95% Wilson score interval for a binomial proportion."""
    p = wins / n
    denom = 1 + Z * Z / n
    center = (p + Z * Z / (2 * n)) / denom
    half = Z * math.sqrt(p * (1 - p) / n + Z * Z / (4 * n * n)) / denom
    return center - half, center + half


class Budget:
    """A hard cap on the number of worker threads running at the same time."""

    def __init__(self, total):
        self.total = total
        self.free = total
        self.weights = {}
        self.cv = threading.Condition()

    def register(self, key, weight):
        with self.cv:
            self.weights[key] = weight

    def finish(self, key):
        with self.cv:
            self.weights.pop(key, None)
            self.cv.notify_all()

    @contextlib.contextmanager
    def lease(self, key):
        """Yields the number of threads the caller may use; they are returned on exit."""
        with self.cv:
            while True:
                share = max(1, round(self.total * self.weights[key] / sum(self.weights.values())))
                grant = min(share, self.free)
                if grant >= 1:
                    break
                self.cv.wait()
            self.free -= grant
        try:
            yield grant
        finally:
            with self.cv:
                self.free += grant
                self.cv.notify_all()


class Tuner:
    def __init__(self, args, budget, log_file, log_lock, mode, mode_name, level, w, h, target):
        self.args, self.budget, self.log_file, self.log_lock = args, budget, log_file, log_lock
        self.target = target
        self.mode, self.mode_name, self.level, self.w, self.h = mode, mode_name, level, w, h
        self.label = "%s %s %dx%d" % (mode_name, level, w, h)
        self.next_seed = args.seed_base
        self.cache = {}
        self.max_mines = w * h - 10  # the first click needs a mine-free 3x3 area

    def evaluate(self, mines, games):
        """Win rate of `games` fresh games (a seed is never reused)."""
        key = (mines, games)
        if key in self.cache:
            return self.cache[key]
        seed0 = self.next_seed
        self.next_seed += games
        started = time.time()
        with self.budget.lease(self) as threads:
            out = subprocess.run(
                [str(self.args.binary), str(self.mode), str(self.w), str(self.h), str(mines), str(games),
                 str(threads), str(seed0), "random"],
                check=True, capture_output=True, text=True).stdout.split()
        wins, played, stuck = int(out[0]), int(out[1]), int(out[2])
        lo, hi = wilson(wins, played)
        res = {"mode": self.mode_name, "level": self.level, "width": self.w, "height": self.h,
               "mines": mines, "games": played, "wins": wins, "stuck": stuck, "rate": wins / played,
               "ci_low": lo, "ci_high": hi, "target": self.target, "contains_target": lo <= self.target <= hi,
               "threads": threads,
               "seconds": round(time.time() - started, 1), "first_seed": seed0}
        with self.log_lock:
            self.log_file.write(json.dumps(res) + "\n")
            self.log_file.flush()
        log(self.label, "%3d mines: %4d/%-4d = %5.1f%%  CI [%4.1f%%, %4.1f%%]  (%d threads, %.0fs)%s%s" % (
            mines, wins, played, 100 * res["rate"], 100 * lo, 100 * hi, threads, res["seconds"],
            "  <-- contains %.0f%%" % (100 * self.target) if res["contains_target"] else "",
            "  STUCK=%d" % stuck if stuck else ""))
        self.cache[key] = res
        return res

    def run(self):
        self.budget.register(self, self.w * self.h)
        try:
            return self._search()
        finally:
            self.budget.finish(self)

    def _search(self):
        # 1. Coarse bisection. Invariant: the rate at `lo` is >= the target, the rate at `hi` is below it.
        lo, hi = 1, self.max_mines
        while hi - lo > 1:
            mid = (lo + hi) // 2
            if self.evaluate(mid, self.args.coarse_games)["rate"] >= self.target:
                lo = mid
            else:
                hi = mid
        # 2. Verification walk, with smaller and smaller samples if the walk gets bracketed.
        mines = lo
        for games in self.args.verify_games:
            visited = set()
            while 1 <= mines <= self.max_mines and mines not in visited:
                visited.add(mines)
                res = self.evaluate(mines, games)
                if res["contains_target"]:
                    log(self.label, "RESULT: %d mines (%.1f%%, CI [%.1f%%, %.1f%%], %d games)" % (
                        mines, 100 * res["rate"], 100 * res["ci_low"], 100 * res["ci_high"], games))
                    return res
                step = 1 if res["rate"] > self.target else -1
                if (mines + step) in visited:
                    break  # bracketed: neighbors are on opposite sides and neither qualifies
                mines += step
        # No interval ever contained the target: fall back to the closest evaluated rate.
        best = min(self.cache.values(), key=lambda r: (abs(r["rate"] - self.target), -r["games"]))
        log(self.label, "WARNING: no CI contained the target; using %d mines (%.1f%%, %d games)" % (
            best["mines"], 100 * best["rate"], best["games"]))
        return best


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--binary", default=str(ROOT / "cpp" / "build" / "winrate"))
    parser.add_argument("--total-threads", type=int, default=os.cpu_count() or 1,
                        help="hard cap on busy worker threads (default: all CPUs)")
    parser.add_argument("--coarse-games", type=int, default=300, help="games per coarse bisection step")
    parser.add_argument("--verify-games", default="2000,1000,500,250",
                        help="comma separated sample sizes for the verification, biggest first")
    parser.add_argument("--seed-base", type=int, default=1, help="first seed; every evaluation uses fresh seeds")
    parser.add_argument("--targets", default=DEFAULT_TARGETS,
                        help="target win rates of Beginner,Intermediate,Expert (default: %(default)s)")
    parser.add_argument("--only", help="run a single configuration, e.g. 'complex Beginner'")
    parser.add_argument("--json", help="write the final table to this file")
    parser.add_argument("--log", default="tuning_log.jsonl", help="append every evaluation to this file")
    args = parser.parse_args()
    args.verify_games = [int(x) for x in args.verify_games.split(",")]
    targets = [float(x) for x in args.targets.split(",")]

    budget = Budget(args.total_threads)
    log_lock = threading.Lock()
    with open(args.log, "a", encoding="utf-8") as log_file:
        jobs = []
        for mode, mode_name in MODES:
            for (level, w, h), target in zip(SIZES, targets):
                if args.only and args.only != "%s %s" % (mode_name, level):
                    continue
                jobs.append(Tuner(args, budget, log_file, log_lock, mode, mode_name, level, w, h, target))
        log("tuner", "%d jobs, %d worker threads in total" % (len(jobs), args.total_threads))
        with ThreadPoolExecutor(max_workers=len(jobs)) as pool:
            results = list(pool.map(Tuner.run, jobs))

    table = {}
    print("\nmode        level         size    target  mines  win rate   95% CI           games")
    for job, res in zip(jobs, results):
        table.setdefault(job.mode_name, {})[job.level] = {
            k: res[k] for k in ("width", "height", "mines", "games", "wins", "rate", "ci_low", "ci_high",
                                "target", "contains_target")}
        print("%-11s %-13s %2dx%-2d   %5.0f%%   %5d  %6.1f%%   [%4.1f%%, %4.1f%%]   %5d%s" % (
            job.mode_name, job.level, job.w, job.h, 100 * job.target, res["mines"], 100 * res["rate"],
            100 * res["ci_low"], 100 * res["ci_high"], res["games"],
            "" if res["contains_target"] else "   (CI does not contain the target)"))
    if args.json:
        Path(args.json).write_text(json.dumps(table, indent=2))
        print("\nwritten to", args.json)


if __name__ == "__main__":
    main()
