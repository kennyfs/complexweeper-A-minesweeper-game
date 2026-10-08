// Self-test for the solver and the undo history.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "session.hpp"

using namespace cw;

namespace {

int g_failed = 0;
int g_checks = 0;

void expect(bool cond, const char* name) {
    ++g_checks;
    if (!cond) {
        ++g_failed;
        std::printf("  [FAIL] %s\n", name);
    }
}

struct Config {
    const char* label;
    std::uint16_t w, h, mines;
    Mode mode;
};

struct Stats {
    int games = 0, wins = 0, losses = 0, stuck = 0;
    int certain_wrong = 0;  // a "certain" step that was wrong (must be 0)
    int steps = 0, guesses = 0, opens = 0, marks = 0;
    int risky_guesses = 0;  // guesses with an estimated risk above 50%
};

// Plays whole games and verifies every step on the way.
Stats playOut(const Config& cfg, std::uint32_t seed_from, int count) {
    Stats st;
    for (int k = 0; k < count; ++k) {
        Session s;
        s.newGame(cfg.w, cfg.h, cfg.mines, cfg.mode, seed_from + static_cast<std::uint32_t>(k));
        ++st.games;
        for (std::size_t guard = 0; guard < s.game.n * 4; ++guard) {
            if (s.game.over) break;
            const Move m = s.solverStep(0);
            if (m.kind == MoveKind::none) {
                ++st.stuck;
                break;
            }
            ++st.steps;
            const bool is_mine = s.game.mine[m.cell] != 0;
            if (m.reason == Reason::guess) {
                ++st.guesses;
                st.risky_guesses += m.risk > 0.5f;
            } else if (m.kind == MoveKind::open) {
                if (is_mine) ++st.certain_wrong;
            } else if (!is_mine) {
                ++st.certain_wrong;
            }
            (m.kind == MoveKind::open ? st.opens : st.marks) += 1;
        }
        st.wins += s.game.over && s.game.win;
        st.losses += s.game.over && !s.game.win;
    }
    return st;
}

void testSoundness() {
    const Config cfgs[] = {
        {"beginner", 9, 9, 10, Mode::complex},          {"intermediate", 16, 16, 40, Mode::complex},
        {"expert", 30, 16, 99, Mode::complex},          {"beginner (Mink.)", 9, 9, 10, Mode::hyper},
        {"intermed. (Mink.)", 16, 16, 40, Mode::hyper}, {"expert (Mink.)", 30, 16, 99, Mode::hyper},
    };
    for (const Config& cfg : cfgs) {
        const auto t0 = std::chrono::steady_clock::now();
        const Stats st = playOut(cfg, 1000, cfg.w >= 30 ? 15 : (cfg.w >= 16 ? 40 : 100));
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        std::printf("  %-17s %3d games: won %3d lost %3d stuck %d | %d steps (open %d, mark %d, guess %d, risk>50%%: %d) | %.0f ms\n",
                    cfg.label, st.games, st.wins, st.losses, st.stuck, st.steps, st.opens, st.marks, st.guesses,
                    st.risky_guesses, ms);
        expect(st.certain_wrong == 0, "certain steps are always right: opened cells are safe, marked cells are mines");
        expect(st.stuck == 0, "the solver always has a next step while the game is running");
        expect(st.wins + st.losses == st.games, "every game reaches an end");
        expect(st.losses <= st.guesses, "no guesses means no losses");
        expect(st.risky_guesses * 10 <= st.guesses, "a guess is rarely worse than a coin flip (<= 10% of guesses)");
    }
}

struct Frozen {
    std::vector<std::uint8_t> open, flag, mark;
    bool started, over, win;
    std::size_t depth;
};

Frozen freeze(const Session& s) {
    Frozen f;
    for (std::size_t i = 0; i < s.game.n; ++i) {
        f.open.push_back(s.game.open[i]);
        f.flag.push_back(s.game.flag[i]);
        f.mark.push_back(s.solver.isMarked(i));
    }
    f.started = s.game.started;
    f.over = s.game.over;
    f.win = s.game.win;
    f.depth = s.undoDepth();
    return f;
}

bool same(const Frozen& a, const Frozen& b) {
    return a.open == b.open && a.flag == b.flag && a.mark == b.mark && a.started == b.started && a.over == b.over &&
           a.win == b.win && a.depth == b.depth;
}

void testUndo() {
    for (Mode mode : {Mode::complex, Mode::hyper}) {
        Session s;
        s.newGame(16, 16, 40, mode, 77);
        std::vector<Frozen> before;  // the state just before each action
        std::uint32_t rnd = 12345;
        const auto next = [&] {
            rnd = rnd * 1103515245u + 12345u;
            return (rnd >> 16) & 0x7fff;
        };
        int performed = 0;
        for (int i = 0; i < 80 && !s.game.over; ++i) {
            const Frozen f = freeze(s);
            bool changed = false;
            const unsigned r = next() % 4;
            const std::size_t cell = next() % s.game.n;
            if (r == 0 && !s.game.started) changed = s.click(cell, 0);
            else if (r == 1) changed = s.cycleFlag(cell);
            else if (r == 2) changed = s.chord(cell);
            else changed = s.solverStep(0).kind != MoveKind::none;
            if (changed) {
                before.push_back(f);
                ++performed;
            }
        }
        expect(performed > 10, "the undo test performs enough actions");
        expect(s.undoDepth() == before.size(), "each effective action leaves one history entry, ineffective ones none");
        // Undo all the way back; every step must restore exactly the state before the action.
        bool all_same = true;
        while (!before.empty()) {
            all_same = all_same && s.undo() && same(freeze(s), before.back());
            before.pop_back();
        }
        expect(all_same, "repeated undo restores the state before each action");
        expect(!s.undo(), "undo returns false when the history is empty");
        expect(!s.game.started, "after undoing everything the game is back to not started");
    }
    std::printf("  undo: done\n");
}

void testStepGranularity() {
    // One step does one thing: it either marks exactly one mine or opens cells (a cascade counts
    // as one open), never both.
    Session s;
    s.newGame(16, 16, 40, Mode::complex, 5);
    bool ok = true;
    while (!s.game.over) {
        const std::size_t marks_before = s.solver.markCount();
        const std::size_t open_before = s.game.openedCount();
        const Move m = s.solverStep(0);
        if (m.kind == MoveKind::none) break;
        if (m.kind == MoveKind::mark) {
            ok = ok && s.solver.markCount() == marks_before + 1 && s.game.openedCount() == open_before;
        } else {
            ok = ok && s.solver.markCount() == marks_before && s.game.openedCount() > open_before;
        }
    }
    expect(ok, "a step opens one cell (with its cascade) or marks one mine");
    std::printf("  step granularity: done\n");
}

void testManualInterplay() {
    // Flags placed by the player must not block the solver; undo still works afterwards.
    Session s;
    s.newGame(9, 9, 10, Mode::complex, 3);
    s.click(40, 0);
    for (std::size_t i = 0; i < s.game.n; ++i) s.cycleFlag(i);  // flag every closed cell
    const int depth = static_cast<int>(s.undoDepth());
    int steps = 0;
    while (!s.game.over && steps < 500) {
        if (s.solverStep(0).kind == MoveKind::none) break;
        ++steps;
    }
    expect(steps > 0 && s.game.over, "the solver finishes a board that is full of flags");
    while (s.undoDepth() > 0) s.undo();
    expect(!s.game.started && depth > 0, "everything can be undone");
    std::printf("  manual actions mixed with solver steps: done\n");
}

}  // namespace

int main() {
    std::printf("Complexweeper solver self-test\n==============================\n");
    testSoundness();
    testUndo();
    testStepGranularity();
    testManualInterplay();
    std::printf("\n%d checks, %d failed\n%s\n", g_checks, g_failed, g_failed == 0 ? "ALL PASSED" : "FAILURES");
    return g_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
