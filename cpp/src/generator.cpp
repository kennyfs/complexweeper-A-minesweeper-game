#include "generator.hpp"

#include <cstdlib>
#include <utility>
#include <vector>

#include "solver.hpp"

namespace cw {
namespace {

constexpr int kMaxEvaluations = 20000;  // checks of one layout before starting over
constexpr int kMaxRestarts = 4;

struct Verdict {
    std::size_t left = 0;                 // safe cells logic could not open
    std::vector<std::uint16_t> frontier;  // undecided cells next to an open cell
    std::vector<std::uint16_t> interior;  // undecided cells with no open neighbor
};

// Plays the layout in g.mine out by logic alone on a scratch copy of the game.
Verdict check(const Game& g, std::size_t start) {
    Game t = g;
    t.computeClues();
    t.countTypes();
    std::fill_n(t.open.begin(), t.n, 0);
    std::fill_n(t.flag.begin(), t.n, 0);
    const std::uint16_t seeds[] = {static_cast<std::uint16_t>(start)};
    t.cascadeOpen(seeds);
    t.started = true;
    t.over = false;
    t.win = false;
    Solver solver;
    Verdict v;
    v.left = solver.solveByLogic(t);
    if (v.left == 0) return v;
    for (std::uint16_t c : solver.undecided(t)) {
        bool next_to_open = false;
        for (std::uint16_t j : t.nbrs(c)) next_to_open = next_to_open || t.open[j] != 0;
        (next_to_open ? v.frontier : v.interior).push_back(c);
    }
    return v;
}

bool improve(Game& g, std::size_t start) {
    const int sr = static_cast<int>(start / g.w);
    const int sc = static_cast<int>(start % g.w);
    const auto banned = [&](std::size_t c) {
        return std::abs(static_cast<int>(c / g.w) - sr) <= 1 && std::abs(static_cast<int>(c % g.w) - sc) <= 1;
    };
    bool custom_types = false;
    for (std::size_t t = 1; t <= 4; ++t) custom_types = custom_types || g.type_count[t] != 0;
    const auto pick = [&](const std::vector<std::uint16_t>& v) { return v[g.rng.below(v.size())]; };

    Verdict cur = check(g, start);
    for (int evals = 1, tries = 0; cur.left != 0 && evals < kMaxEvaluations && tries < 20 * kMaxEvaluations; ++tries) {
        std::vector<std::uint16_t> all = cur.frontier;
        all.insert(all.end(), cur.interior.begin(), cur.interior.end());
        if (all.empty()) return false;
        if (cur.frontier.empty()) cur.frontier = all;
        auto cand = g.mine;
        const std::size_t kind = g.rng.below(10);
        if (kind < 5) {  // a mine off the border into the interior
            const std::size_t a = pick(cur.frontier);
            const std::size_t b = cur.interior.empty() ? pick(all) : pick(cur.interior);
            if (cand[a] == 0 || cand[b] != 0) continue;
            std::swap(cand[a], cand[b]);
        } else if (kind < 7) {  // two cells of the border trade places
            const std::size_t a = pick(cur.frontier);
            const std::size_t b = pick(cur.frontier);
            std::swap(cand[a], cand[b]);
        } else if (kind < 9) {  // another type for a mine
            const std::size_t a = pick(all);
            if (cand[a] == 0 || custom_types) continue;
            cand[a] = static_cast<std::uint8_t>(1 + g.rng.below(4));
        } else {  // a mine from anywhere onto the border
            const std::size_t a = pick(cur.frontier);
            std::size_t b;
            do {
                b = g.rng.below(g.n);
            } while (banned(b));
            if (cand[a] != 0 || cand[b] == 0) continue;
            std::swap(cand[a], cand[b]);
        }
        if (cand == g.mine) continue;
        const auto before = g.mine;
        g.mine = cand;
        Verdict next = check(g, start);
        ++evals;
        if (next.left <= cur.left) {
            cur = std::move(next);
        } else {
            g.mine = before;
        }
    }
    return cur.left == 0;
}

}  // namespace

void makeSolvable(Game& g, std::size_t start) {
    for (int attempt = 0;; ++attempt) {
        if (improve(g, start) || attempt == kMaxRestarts) return;
        g.layMines(start);
    }
}

}  // namespace cw
