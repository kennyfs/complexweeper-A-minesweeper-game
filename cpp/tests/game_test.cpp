// Rules self-test: runs the rule-level invariants (no UI).
#include <algorithm>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <vector>

#include "game.hpp"

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

using Flags = std::array<bool, MAX_CELLS>;
using Counts = std::array<std::uint16_t, 5>;
constexpr Counts kRandom{};  // all zero = random mine types

// An independent flood fill used to check the cascade:
// cascade coverage = connected blank cells + their non-mine neighbors.
Flags expectedCascade(const Game& g, std::size_t start) {
    Flags set{};
    Flags comp{};
    std::vector<std::size_t> stack{start};
    comp[start] = true;
    while (!stack.empty()) {
        const std::size_t i = stack.back();
        stack.pop_back();
        for (std::uint16_t j : g.nbrs(i)) {
            if (comp[j] || g.mine[j] != 0) continue;
            if (g.isBlank(j)) {
                comp[j] = true;
                stack.push_back(j);
            }
        }
    }
    for (std::size_t i = 0; i < g.n; ++i) {
        if (!comp[i]) continue;
        set[i] = true;
        for (std::uint16_t j : g.nbrs(i)) {
            if (g.mine[j] == 0) set[j] = true;
        }
    }
    return set;
}

void buildBoard(Game& g, std::uint16_t w, std::uint16_t h, std::uint16_t mines, const Counts& tc,
                std::uint32_t seed, std::size_t start) {
    g.w = w;
    g.h = h;
    g.mines = mines;
    g.type_count = tc;
    g.newGame(seed);
    g.startAt(start, 0);
}

Flags openSnapshot(const Game& g) {
    Flags f{};
    for (std::size_t i = 0; i < g.n; ++i) f[i] = g.open[i] != 0;
    return f;
}

// Minkowski tests: cell 12 of a 9x9 board has exactly 8 neighbors; place mines (or flags) on
// them according to the number of mines of each of the four types.
// With as_flag = true flags are placed (clues are not recomputed), otherwise real mines are
// placed and the clues are recomputed.
void hyperPlace(Game& g, const std::array<int, 4>& counts, bool as_flag) {
    for (std::size_t i = 0; i < 25; ++i) {
        if (!as_flag) g.mine[i] = 0;
        g.flag[i] = 0;
        g.open[i] = 0;
        g.clue[i] = -1;
    }
    const Nbrs nb = g.nbrs(12);
    std::size_t slot = 0;
    for (std::size_t t = 1; t <= 4; ++t) {
        for (int j = 0; j < counts[t - 1] && slot < nb.size(); ++j, ++slot) {
            if (as_flag) {
                g.setFlag(nb.cells[slot], static_cast<std::uint8_t>(t));
            } else {
                g.mine[nb.cells[slot]] = static_cast<std::uint8_t>(t);
            }
        }
    }
    if (!as_flag) g.computeClues();
}

// ---- 1. Determinism of the random numbers ----
void testDeterminism() {
    Game a, b, c;
    buildBoard(a, 16, 16, 40, kRandom, 12345, 100);
    buildBoard(b, 16, 16, 40, kRandom, 12345, 100);
    buildBoard(c, 16, 16, 40, kRandom, 999, 100);
    bool same = true;
    bool diff = false;
    for (std::size_t i = 0; i < a.n; ++i) {
        same = same && a.mine[i] == b.mine[i];
        diff = diff || a.mine[i] != c.mine[i];
    }
    expect(same, "same seed + same first click give the identical board");
    expect(diff, "different seeds give different boards");

    Rng r1(42), r2(42);
    bool in_range = true;
    for (int i = 0; i < 1000; ++i) {
        const double x = r1.next();
        in_range = in_range && x == r2.next() && x >= 0.0 && x < 1.0;
    }
    expect(in_range, "Rng: same seed gives the same sequence, within [0, 1)");
    Rng r3(7);
    bool below_ok = true;
    for (int i = 0; i < 1000; ++i) below_ok = below_ok && r3.below(10) < 10;
    expect(below_ok && r3.below(0) == 0, "below(n) < n, and below(0) == 0");
    Rng z0(0), z1(1);
    expect(z0.next() == z1.next(), "seed 0 is equivalent to seed 1");

    bool split_ok = splitEvenly(10) == Counts{0, 3, 3, 2, 2};
    for (std::uint16_t total = 0; total <= MAX_MINES; ++total) {
        const Counts s = splitEvenly(total);
        split_ok = split_ok && s[1] + s[2] + s[3] + s[4] == total && s[1] - s[4] <= 1;
    }
    expect(split_ok, "splitEvenly conserves the total and the types differ by at most 1");
    std::printf("1 determinism: done\n");
}

// ---- 2. Exact mine-type ratio ----
void testExactRatio() {
    const Counts cases[] = {{0, 3, 2, 4, 1}, {0, 0, 0, 0, 6}, {0, 7, 0, 0, 0}, {0, 5, 5, 0, 0}, {0, 0, 0, 4, 4}};
    int bad = 0;
    Game game;
    for (const Counts& tc : cases) {
        std::uint16_t sum = 0;
        for (std::size_t t = 1; t <= 4; ++t) sum = static_cast<std::uint16_t>(sum + tc[t]);
        for (std::uint32_t seed : {11u, 22u, 33u}) {
            buildBoard(game, 12, 12, sum, tc, seed, 70);
            for (std::size_t t = 1; t <= 4; ++t) bad += game.type_total[t] != tc[t];
            bad += game.typeSum() != sum;
            bad += game.mines != sum;
        }
    }
    expect(bad == 0, "a requested ratio is honored exactly (5 ratios x 3 seeds)");
    std::printf("2 exact ratio: %s\n", bad == 0 ? "ok" : "deviates");
}

// ---- 3. Pure real / pure imaginary boards only show perfect squares ----
void testPureSquares() {
    int bad = 0;
    Game game;
    for (const Counts& tc : {Counts{0, 5, 5, 0, 0}, Counts{0, 0, 0, 4, 4}}) {
        std::uint16_t sum = 0;
        for (std::size_t t = 1; t <= 4; ++t) sum = static_cast<std::uint16_t>(sum + tc[t]);
        for (std::uint32_t seed : {7u, 8u, 9u}) {
            buildBoard(game, 12, 12, sum, tc, seed, 70);
            for (std::size_t i = 0; i < game.n; ++i) {
                if (game.mine[i] != 0) continue;
                const int D = game.clue[i];
                int r = 0;
                while (r * r < D) ++r;
                bad += r * r != D;
            }
        }
    }
    expect(bad == 0, "all displayed values of pure real / pure imaginary boards are perfect squares");
    std::printf("3 pure real / imaginary invariant: %s\n", bad == 0 ? "ok" : "failed");
}

// ---- 4. Cascade: matches an independent flood fill and never opens a mine ----
void testCascade() {
    Game game;
    int mismatch = 0, mine_opened = 0, samples = 0, zero_cascaded = 0, zero_samples = 0;
    for (std::uint32_t seed : {301u, 302u, 303u, 304u, 305u}) {
        buildBoard(game, 12, 12, 24, kRandom, seed, 70);
        for (std::size_t i = 0; i < game.n && samples < 40; ++i) {
            if (game.mine[i] != 0 || game.open[i] != 0 || !game.isBlank(i)) continue;
            buildBoard(game, 12, 12, 24, kRandom, seed, 70);
            if (game.open[i] != 0) continue;
            const Flags before = openSnapshot(game);
            game.reveal(i);
            const Flags want = expectedCascade(game, i);
            ++samples;
            for (std::size_t k = 0; k < game.n; ++k) {
                const bool got = game.open[k] != 0 && !before[k];
                const bool exp = want[k] && !before[k];
                mismatch += got != exp;
                mine_opened += got && game.mine[k] != 0;
            }
        }
        // A cell showing 0 with mines around it (cancelling pairs) never cascades.
        buildBoard(game, 12, 12, 24, kRandom, seed, 70);
        for (std::size_t k = 0; k < game.n; ++k) {
            if (game.mine[k] != 0 || game.open[k] != 0) continue;
            if (game.clue[k] != 0 || game.nbrMineCount(k) == 0) continue;
            const std::size_t before = game.openedCount();
            game.reveal(k);
            ++zero_samples;
            zero_cascaded += game.openedCount() - before != 1;
        }
    }
    expect(samples >= 20, "enough blank-cell cascade samples");
    expect(mismatch == 0, "cascade matches the independent flood fill cell by cell");
    expect(mine_opened == 0, "a cascade never opens a mine");
    expect(zero_samples >= 5, "enough samples of cells showing 0");
    expect(zero_cascaded == 0, "a cell showing 0 never cascades");
    std::printf("4 cascade: %d blank samples, %d zero samples, %d mismatches, %d mines opened\n", samples,
                zero_samples, mismatch, mine_opened);
}

// ---- 4b. Opening cascade on big boards ----
void testBigCascade() {
    struct Shape {
        std::uint16_t w, h, m;
    };
    const Shape shapes[] = {{40, 30, 1}, {40, 30, 5}, {40, 30, 20}, {40, 30, 60}, {40, 20, 40}, {30, 30, 60}};
    int mismatch = 0, opened_mine = 0, samples = 0, big = 0;
    std::size_t worst = 0;
    Game game;
    for (const Shape& sh : shapes) {
        const std::size_t bw = sh.w;
        const std::size_t bh = sh.h;
        const std::size_t starts[] = {0, bw / 2, (bh / 2) * bw + bw / 2, bw * bh - 1};
        for (std::uint32_t seed : {4111u, 4127u, 4133u, 4139u, 4153u, 4159u}) {
            for (std::size_t st : starts) {
                buildBoard(game, sh.w, sh.h, sh.m, kRandom, seed, st);
                const Flags want = expectedCascade(game, st);
                for (std::size_t k = 0; k < game.n; ++k) {
                    mismatch += (game.open[k] != 0) != want[k];
                    opened_mine += game.open[k] != 0 && game.mine[k] != 0;
                }
                const std::size_t oc = game.openedCount();
                worst = std::max(worst, oc);
                big += oc >= 400;
                ++samples;
            }
        }
    }
    expect(samples >= 72, "enough big-board samples (6 shapes x 6 seeds x 4 start cells)");
    expect(big >= 8, "some samples are real big cascades (>= 400 cells)");
    expect(mismatch == 0, "big-board opening cascade matches the independent flood fill");
    expect(opened_mine == 0, "big-board cascades never open a mine");
    std::printf("4b big cascade: %d samples, %d big cascades, at most %zu cells at once, %d mismatches\n", samples,
                big, worst, mismatch);
}

// ---- 5. The opening click always cascades and is safe ----
void testStart() {
    int bad = 0;
    for (std::uint32_t seed : {501u, 502u, 503u, 504u, 505u}) {
        Game gm;
        gm.w = 16;
        gm.h = 16;
        gm.mines = 40;
        gm.newGame(seed);
        const std::size_t start = 16 * 8 + 8;
        gm.startAt(start, 0);
        bad += gm.over;
        bad += gm.openedCount() < 9;
        bad += !gm.isBlank(start);
        bad += gm.typeSum() != 40;
        for (std::size_t t = 1; t <= 4; ++t) {
            bad += gm.type_total[t] == 0 && gm.mines >= 40;
            bad += gm.unmarked(t) != static_cast<std::int32_t>(gm.type_total[t]);
        }
    }
    expect(bad == 0, "start cell is blank, cascades (>= 9 cells), is safe; per-type counts are known");
    std::printf("5 opening cascade and per-type counts: %s\n", bad == 0 ? "ok" : "failed");
}

// ---- 6. Chord criterion (complex mode) matches an independent implementation ----
void testJudge() {
    int mismatch = 0, pass = 0, total = 0;
    Game game;
    for (std::uint32_t seed : {601u, 602u, 603u}) {
        buildBoard(game, 12, 12, 24, kRandom, seed, 70);
        for (std::size_t i = 0; i < game.n; ++i) {
            if (game.mine[i] != 0 || game.open[i] == 0) continue;
            const Nbrs nb = game.nbrs(i);
            for (std::uint16_t j : nb) {
                if (game.open[j] != 0) continue;
                game.setFlag(j, static_cast<std::uint8_t>(1 + ((i + j) % 4)));
            }
            std::array<int, 4> truth{}, got{};
            for (std::uint16_t j : nb) {
                if (game.mine[j] != 0) ++truth[game.mine[j] - 1];
                if (game.flag[j] != 0) ++got[game.flag[j] - 1];
            }
            const int P = truth[0] + truth[1], V = truth[2] + truth[3];
            const int gp = got[0] + got[1], gv = got[2] + got[3];
            const bool want = gp + gv == P + V && ((gp == P && gv == V) || (gp == V && gv == P));
            ++total;
            mismatch += want != game.matchComboTruth(i);
            pass += want;
        }
        for (std::size_t j = 0; j < game.n; ++j) game.setFlag(j, 0);
    }
    expect(total > 50, "enough criterion samples");
    expect(mismatch == 0, "the criterion matches the independent implementation");
    expect(pass > 0, "the criterion lets some combinations through");
    std::printf("6 combination criterion: %d samples, %d accepted, %d mismatches\n", total, pass, mismatch);
}

// ---- 7. Unlimited flags + cycle order ----
void testFlags() {
    Game game;
    buildBoard(game, 9, 9, 10, kRandom, 701, 40);
    unsigned placed = 0;
    for (std::size_t i = 0; i < game.n; ++i) {
        if (game.open[i] == 0 && game.setFlag(i, 1)) ++placed;
    }
    expect(placed > 0, "every closed cell can be flagged");
    expect(game.flags_of[1] == placed, "the counter matches the number of flags placed");
    expect(game.unmarked(1) < 0, "over-flagging makes the unmarked count negative");
    expect(game.flagsTotal() == placed, "flagsTotal matches the number of flags placed");

    std::size_t cell = 0;
    while (game.open[cell] != 0) ++cell;
    game.setFlag(cell, 0);
    std::array<std::uint8_t, 6> seq{};
    for (auto& s : seq) {
        game.cycleFlag(cell);
        s = game.flag[cell];
    }
    expect((seq == std::array<std::uint8_t, 6>{1, 2, 3, 4, 0, 1}), "right-click cycle is 1,2,3,4,0,1");
    std::printf("7 unlimited flags: %u placed\n", placed);
}

// ---- 8. A flag protects its cell ----
void testFlagProtection() {
    Game game;
    buildBoard(game, 9, 9, 10, kRandom, 801, 40);
    std::size_t cell = 0;
    while (!(game.open[cell] == 0 && game.mine[cell] == 0)) ++cell;
    game.setFlag(cell, 3);
    expect(game.flags_of[3] == 1, "the counter is 1 after placing a flag");
    game.reveal(cell);
    expect(game.open[cell] == 0, "a flagged cell cannot be opened");
    expect(game.flag[cell] == 3, "the flag is kept when the cell cannot be opened");
    expect(game.flags_of[3] == 1, "the counter does not change when the cell cannot be opened");

    // A cascade must not eat flags either.
    std::size_t blank = 0, flagged_nbr = 0;
    bool found = false;
    for (std::size_t i = 0; i < game.n && !found; ++i) {
        if (game.open[i] != 0 || game.mine[i] != 0 || !game.isBlank(i)) continue;
        for (std::uint16_t j : game.nbrs(i)) {
            if (game.open[j] == 0 && game.mine[j] == 0 && game.flag[j] == 0) {
                blank = i;
                flagged_nbr = j;
                found = true;
                break;
            }
        }
    }
    if (found) {
        game.setFlag(flagged_nbr, 1);
        game.reveal(blank);
        expect(game.open[blank] == 1, "a blank cell can be opened");
        expect(game.open[flagged_nbr] == 0, "a cascade does not open a flagged cell");
        expect(game.flag[flagged_nbr] == 1, "a cascade does not clear a flag");
    }
    game.setFlag(cell, 0);
    game.reveal(cell);
    expect(game.open[cell] == 1, "after removing the flag the cell can be opened");
    expect(game.flags_of[3] == 0, "removing the flag gives the count back");
    std::printf("8 flags protect cells (cannot open, cascades leave them alone): done\n");
}

// ---- 9. Win / lose ----
void testWinLose() {
    Game game;
    buildBoard(game, 9, 9, 10, kRandom, 901, 40);
    for (std::size_t i = 0; i < game.n; ++i) {
        if (game.mine[i] == 0 && game.open[i] == 0) game.reveal(i);
    }
    expect(game.win && game.over, "opening every safe cell wins");
    expect(game.msg == Msg::win, "feedback on a win is win");
    expect(game.openedCount() == game.safeCount(), "on a win the number of open cells equals the number of safe cells");

    buildBoard(game, 9, 9, 10, kRandom, 902, 40);
    std::size_t left = 0;
    while (!(game.mine[left] == 0 && game.open[left] == 0)) ++left;
    for (std::size_t i = 0; i < game.n; ++i) {
        if (i != left && game.mine[i] == 0 && game.open[i] == 0) game.reveal(i);
    }
    expect(!game.win, "no win while a safe cell is still closed");

    buildBoard(game, 9, 9, 10, kRandom, 903, 40);
    std::size_t m = 0;
    while (game.mine[m] == 0) ++m;
    game.reveal(m);
    expect(game.over && !game.win, "opening a mine loses");
    expect(game.boom == static_cast<std::int32_t>(m), "the cell that was stepped on is recorded");
    expect(game.msg == Msg::lose, "feedback on a loss is lose");
    game.reveal(0);  // actions after the end must not change the position
    expect(game.boom == static_cast<std::int32_t>(m), "actions after the end have no effect");
    std::printf("9 win / lose: done\n");
}

// ---- 10. Chord: a failed criterion leaves the board unchanged ----
void testExpandGate() {
    Game game;
    buildBoard(game, 12, 12, 24, kRandom, 1001, 70);
    std::size_t cell = 0;
    for (std::size_t i = 0; i < game.n; ++i) {
        if (game.open[i] == 0 || game.mine[i] != 0) continue;
        int uns = 0;
        for (std::uint16_t j : game.nbrs(i)) uns += game.open[j] == 0 && game.flag[j] == 0;
        if (uns > 0) {
            cell = i;
            break;
        }
    }
    const Flags before = openSnapshot(game);
    game.tryExpand(cell);
    if (!game.matchComboTruth(cell)) {
        expect(openSnapshot(game) == before, "a failed criterion must not change the board");
        expect(game.msg == Msg::judge_fail, "a failed criterion gives judge_fail feedback");
    }
    std::printf("10 chord gate: done\n");
}

// ---- 11. Minkowski mode ----
void testHyper() {
    // Enumerate the 495 neighborhoods (counts n1..n4 of the four types, total <= 8) and collect
    // the displayed values of both modes.
    std::array<bool, 65> seen_c{};
    std::array<bool, 129> seen_h{};
    int combos = 0;
    for (int n1 = 0; n1 <= 8; ++n1)
        for (int n2 = 0; n1 + n2 <= 8; ++n2)
            for (int n3 = 0; n1 + n2 + n3 <= 8; ++n3)
                for (int n4 = 0; n1 + n2 + n3 + n4 <= 8; ++n4) {
                    ++combos;
                    const int a = n1 - n2;
                    const int b = n3 - n4;
                    seen_c[a * a + b * b] = true;
                    seen_h[a * a - b * b + 64] = true;
                }
    expect(combos == 495, "495 neighborhood combinations are enumerated");
    const auto cn = std::count(seen_c.begin(), seen_c.end(), true);
    const auto hn = std::count(seen_h.begin(), seen_h.end(), true);
    expect(cn == static_cast<long>(ACHIEVABLE.size()), "complex mode has 24 displayed values");
    expect(hn == 39, "Minkowski mode has 39 displayed values");
    bool cplx_ok = true;
    for (auto D : ACHIEVABLE) cplx_ok = cplx_ok && seen_c[D];
    expect(cplx_ok, "the complex-mode value set is exactly the 24 ACHIEVABLE values");
    const int MAG[] = {1, 3, 4, 5, 7, 8, 9, 12, 15, 16, 21, 24, 25, 32, 35, 36, 48, 49, 64};
    bool pair_ok = seen_h[64];
    for (int m : MAG) pair_ok = pair_ok && seen_h[m + 64] && seen_h[-m + 64];
    expect(pair_ok, "Minkowski mode is 19 magnitudes with both signs, plus a 0");
    std::printf("11 displayed value sets: %d combinations, complex %ld values, Minkowski %ld values\n", combos,
                static_cast<long>(cn), static_cast<long>(hn));

    // Clue formula: a^2 - b^2 (a^2 + b^2 in complex mode); negative values are computed as is.
    Game h;
    h.mode = Mode::hyper;
    struct ClueCase {
        std::array<int, 4> t;
        int want;
    };
    const ClueCase clues[] = {
        {{1, 0, 0, 0}, 1},  {{0, 0, 1, 0}, -1}, {{1, 0, 1, 0}, 0},   {{2, 0, 0, 0}, 4},
        {{0, 2, 0, 0}, 4},  {{1, 0, 2, 0}, -3}, {{0, 0, 2, 0}, -4},  {{2, 1, 3, 1}, -3},
        {{4, 0, 0, 4}, 0},  {{0, 0, 6, 0}, -36}, {{3, 0, 4, 0}, -7}, {{4, 0, 0, 0}, 16},
    };
    int clue_bad = 0;
    for (const auto& c : clues) {
        hyperPlace(h, c.t, false);
        clue_bad += h.clue[12] != c.want;
        clue_bad += !seen_h[c.want + 64];
    }
    expect(clue_bad == 0, "Minkowski clue is a^2 - b^2 (12 mine mixes checked one by one)");
    h.mode = Mode::complex;
    hyperPlace(h, {2, 1, 3, 1}, false);
    expect(h.clue[12] == 5, "the same mix in complex mode is a^2 + b^2 (1 + 4 = 5)");
    h.mode = Mode::hyper;

    // Criterion: truth = one +1 and one +j (a = 1, b = 1, two mines, displayed value 0).
    hyperPlace(h, {1, 0, 1, 0}, false);
    expect(h.clue[12] == 0, "the true mix displays 0");
    hyperPlace(h, {1, 0, 1, 0}, true);
    expect(h.matchComboTruth(12), "the criterion passes when the flags equal the truth");
    hyperPlace(h, {0, 1, 0, 1}, true);
    expect(h.matchComboTruth(12), "the recommended criterion lets a and b each flip sign (four sign combinations)");
    hyperPlace(h, {1, 1, 0, 0}, true);
    expect(!h.matchComboTruth(12), "recommended criterion: a mismatch in |a| or |b| is rejected");
    h.judge_loose = true;
    expect(h.matchComboTruth(12), "loose criterion: the same flag count and the same a^2 - b^2 passes");
    h.judge_loose = false;
    hyperPlace(h, {1, 0, 0, 0}, true);
    expect(!h.matchComboTruth(12), "a flag count different from the mine count is rejected (both criteria)");
    hyperPlace(h, {2, 0, 0, 0}, false);
    hyperPlace(h, {1, 1, 0, 0}, true);
    expect(!h.matchComboTruth(12), "2 flags = 2 mines but |a| differs (2 vs 0) is still rejected");

    // Chord: when the criterion passes, the remaining unflagged neighbors are opened.
    hyperPlace(h, {1, 0, 1, 0}, false);
    hyperPlace(h, {1, 0, 1, 0}, true);
    h.setFlag(80, 1);  // a flag far away: the cascade goes around it, so this chord cannot open everything and win
    h.open[12] = 1;
    h.over = h.win = false;
    h.boom = -1;
    h.setMsg(Msg::none);
    h.tryExpand(12);
    expect(h.msg == Msg::expand_ok && !h.win, "Minkowski chord with a passing criterion gives expand_ok");
    expect(h.msg_arg == 6, "the feedback carries the neighbors involved (8 neighbors - 2 flags = 6)");
    expect(h.boom < 0, "every mine around is flagged, so the chord must not hit a mine");
    bool opened_any = false;
    for (std::size_t i = 0; i < h.n; ++i) opened_any = opened_any || (i != 12 && h.open[i] != 0);
    expect(opened_any, "a passing criterion really opens neighbors");

    // Flags on the wrong cells with a passing criterion: the chord hits a mine and loses.
    hyperPlace(h, {1, 0, 1, 0}, false);
    hyperPlace(h, {0, 0, 0, 0}, true);
    const Nbrs nb = h.nbrs(12);
    std::size_t wrong1 = nb.cells[6], wrong2 = nb.cells[7];  // mines are in slots 0 and 1, flags go elsewhere
    h.setFlag(wrong1, 1);
    h.setFlag(wrong2, 3);
    h.open[12] = 1;
    h.over = h.win = false;
    h.tryExpand(12);
    expect(h.over && !h.win && h.boom >= 0, "flags on the wrong cells with a passing criterion: the chord loses");
    std::printf("11b/11c Minkowski clue formula and criterion: done\n");
}

// ---- 12. Robustness: out-of-range input and extreme parameters ----
void testRobustness() {
    Game g;
    buildBoard(g, 9, 9, 10, kRandom, 1201, 40);
    const Flags before = openSnapshot(g);
    g.reveal(10000);
    g.tryExpand(10000);
    expect(!g.cycleFlag(10000), "cycleFlag out of range returns false");
    expect(!g.setFlag(10000, 1), "setFlag out of range returns false");
    expect(!g.setFlag(0, 5), "setFlag with a type > 4 returns false");
    expect(!g.isBlank(10000) && g.nbrs(10000).size() == 0, "an out-of-range cell has no neighborhood and is not blank");
    expect(openSnapshot(g) == before && !g.over, "out-of-range actions do not change the position");
    const auto seed_before = g.seed;
    g.startAt(10000, 0);
    expect(g.seed == seed_before && g.started, "startAt out of range is a no-op");

    // An excessive custom ratio: no crash, never more mines than available cells.
    Game big;
    big.w = 40;
    big.h = 30;
    big.type_count = {0, 999, 999, 999, 999};
    big.newGame(5);
    big.startAt(0, 0);
    expect(big.mines <= big.n - 4 && big.mines == big.typeSum(), "an excessive ratio is cut to the available cells");
    expect(big.mine[0] == 0 && big.mine[1] == 0 && big.mine[40] == 0 && big.mine[41] == 0,
           "an excessive ratio still keeps the safe zone around the start");

    // Size and mine count are clamped.
    Game c;
    c.w = 0;
    c.h = 500;
    c.mines = 60000;
    c.newGame(1);
    expect(c.w == 1 && c.h == MAX_H && c.n == MAX_H, "w and h are clamped to legal ranges");
    expect(c.mines == MAX_MINES, "mines is clamped to MAX_MINES");

    // Smallest board: 1x1 has no neighbors, opening it wins.
    Game one;
    one.w = 1;
    one.h = 1;
    one.mines = 0;
    one.newGame(1);
    one.startAt(0, 0);
    expect(one.over && one.win, "a 1x1 board without mines is won at the first click");

    // 3x3 with the first click in the center: the safe zone covers the whole board, no mine fits.
    Game tiny;
    tiny.w = 3;
    tiny.h = 3;
    tiny.mines = 5;
    tiny.newGame(1);
    tiny.startAt(4, 0);
    expect(tiny.mines == 0 && tiny.over && tiny.win, "when the safe zone fills the board there are 0 mines and it is won");
    std::printf("12 robustness: done\n");
}

}  // namespace

int main() {
    std::printf("Complexweeper rules self-test (C++)\n===================================\n");
    testDeterminism();
    testExactRatio();
    testPureSquares();
    testCascade();
    testBigCascade();
    testStart();
    testJudge();
    testFlags();
    testFlagProtection();
    testWinLose();
    testExpandGate();
    testHyper();
    testRobustness();

    std::printf("\n%d checks, %d failed\n%s\n", g_checks, g_failed, g_failed == 0 ? "ALL PASSED" : "FAILURES");
    return g_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
