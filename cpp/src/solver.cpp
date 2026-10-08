// Complexweeper solver implementation (see solver.hpp for the overview).
#include "solver.hpp"

#include <algorithm>
#include <bit>
#include <bitset>
#include <cstdlib>
#include <numeric>

namespace cw {
namespace {

// Node budgets for exhaustive enumeration. A whole component gets the larger budget; if that is
// not enough we fall back to many small windows around single constraints.
constexpr std::uint64_t kComponentBudget = 50000;
constexpr std::uint64_t kWindowBudget = 10000;
constexpr int kMaxWindowRadius = 2;
constexpr int kMaxNbr = 8;

int clueValue(Mode mode, int a, int b) {
    return mode == Mode::hyper ? a * a - b * b : a * a + b * b;
}

// reachable(mode, rem, a, b): the mine vectors summed so far give (a, b) and `rem` cells are
// still undecided. Each undecided cell is either empty or one unit step along an axis, so the
// final sum can be any point within L1 distance `rem`. The result is the set of displayed
// values that can still come out (index = value + 64), used to prune the search early.
using Reach = std::bitset<129>;
using ReachTable = std::array<std::array<std::array<std::array<Reach, 17>, 17>, kMaxNbr + 1>, 2>;

const Reach& reachable(Mode mode, int rem, int a, int b) {
    static const ReachTable table = [] {
        ReachTable t{};
        for (int m = 0; m < 2; ++m) {
            const Mode mode_m = m == 1 ? Mode::hyper : Mode::complex;
            for (int r = 0; r <= kMaxNbr; ++r)
                for (int a = -8; a <= 8; ++a)
                    for (int b = -8; b <= 8; ++b)
                        for (int da = -r; da <= r; ++da)
                            for (int db = -(r - std::abs(da)); db <= r - std::abs(da); ++db) {
                                const int v = clueValue(mode_m, a + da, b + db);
                                if (v >= -64 && v <= 64) t[m][r][a + 8][b + 8].set(static_cast<std::size_t>(v + 64));
                            }
        }
        return t;
    }();
    return table[mode == Mode::hyper][rem][a + 8][b + 8];
}

// One constraint = one open number.
struct Con {
    std::array<int, kMaxNbr> vars{};
    int n = 0;
    int target = 0;
    bool need_mine = false;  // shows 0 but is not blank: there are (cancelling) mines around
};

struct Problem {
    Mode mode = Mode::complex;
    std::vector<int> var_of;                // cell -> variable (-1 = not on the frontier)
    std::vector<std::uint16_t> cell;        // variable -> cell
    std::vector<std::uint8_t> dom;          // variable -> bit mask of values still possible
    std::vector<Con> cons;
    std::vector<std::vector<int>> cons_of;  // variable -> constraints that mention it
};

Problem build(const Game& g, const std::array<std::uint8_t, MAX_CELLS>& marks) {
    Problem P;
    P.mode = g.mode;
    P.var_of.assign(g.n, -1);
    const auto var = [&](std::size_t cell) {
        if (P.var_of[cell] < 0) {
            P.var_of[cell] = static_cast<int>(P.cell.size());
            P.cell.push_back(static_cast<std::uint16_t>(cell));
            P.dom.push_back(marks[cell] != 0 ? 0b11110 : 0b11111);  // a marked cell is a mine
            P.cons_of.emplace_back();
        }
        return P.var_of[cell];
    };
    for (std::size_t i = 0; i < g.n; ++i) {
        if (g.open[i] == 0) continue;
        Con c;
        for (std::uint16_t j : g.nbrs(i)) {
            if (g.open[j] == 0) c.vars[c.n++] = var(j);
        }
        if (c.n == 0) continue;
        c.target = g.clue[i];
        const bool blank = c.target == 0 && g.isBlank(i);  // visible to the player
        c.need_mine = c.target == 0 && !blank;
        const int ci = static_cast<int>(P.cons.size());
        for (int k = 0; k < c.n; ++k) {
            if (blank) P.dom[c.vars[k]] &= 1;  // no mines at all around a blank cell
            P.cons_of[c.vars[k]].push_back(ci);
        }
        P.cons.push_back(c);
    }
    return P;
}

// A smaller problem made of a subset of the constraints. back[i] = original variable of sub
// variable i. Variables keep their original relative order.
Problem subProblem(const Problem& P, const std::vector<int>& con_ids, std::vector<int>& back) {
    back.clear();
    for (int ci : con_ids) {
        for (int k = 0; k < P.cons[ci].n; ++k) back.push_back(P.cons[ci].vars[k]);
    }
    std::sort(back.begin(), back.end());
    back.erase(std::unique(back.begin(), back.end()), back.end());

    Problem Q;
    Q.mode = P.mode;
    Q.cell.resize(back.size());
    Q.dom.resize(back.size());
    Q.cons_of.resize(back.size());
    std::vector<int> map(P.cell.size(), -1);
    for (std::size_t i = 0; i < back.size(); ++i) {
        map[back[i]] = static_cast<int>(i);
        Q.cell[i] = P.cell[back[i]];
        Q.dom[i] = P.dom[back[i]];
    }
    for (int ci : con_ids) {
        Con c = P.cons[ci];
        for (int k = 0; k < c.n; ++k) c.vars[k] = map[c.vars[k]];
        const int qi = static_cast<int>(Q.cons.size());
        for (int k = 0; k < c.n; ++k) Q.cons_of[c.vars[k]].push_back(qi);
        Q.cons.push_back(c);
    }
    return Q;
}

// ---- Constraint propagation ----

void supportDfs(const Problem& P, const Con& c, int k, int a, int b, int nz, std::array<int, kMaxNbr>& cur,
                std::array<std::uint8_t, kMaxNbr>& support) {
    if (k == c.n) {
        if (clueValue(P.mode, a, b) == c.target && (!c.need_mine || nz > 0)) {
            for (int i = 0; i < c.n; ++i) support[i] |= static_cast<std::uint8_t>(1u << cur[i]);
        }
        return;
    }
    const std::uint8_t dom = P.dom[c.vars[k]];
    for (int t = 0; t <= 4; ++t) {
        if (((dom >> t) & 1) == 0) continue;
        const int da = t ? TYPES[t - 1][0] : 0;
        const int db = t ? TYPES[t - 1][1] : 0;
        if (!reachable(P.mode, c.n - k - 1, a + da, b + db).test(static_cast<std::size_t>(c.target + 64))) continue;
        cur[k] = t;
        supportDfs(P, c, k + 1, a + da, b + db, nz + (t != 0), cur, support);
    }
}

// Enumerate the legal combinations of each constraint and drop every value no combination
// supports, until nothing changes. Returns false if the information is contradictory.
bool propagate(Problem& P) {
    for (bool changed = true; changed;) {
        changed = false;
        for (const Con& c : P.cons) {
            std::array<int, kMaxNbr> cur{};
            std::array<std::uint8_t, kMaxNbr> support{};
            supportDfs(P, c, 0, 0, 0, 0, cur, support);
            for (int i = 0; i < c.n; ++i) {
                std::uint8_t& d = P.dom[c.vars[i]];
                const auto nd = static_cast<std::uint8_t>(d & support[i]);
                if (nd == 0) return false;
                if (nd != d) {
                    d = nd;
                    changed = true;
                }
            }
        }
    }
    return true;
}

// ---- Exhaustive enumeration ----

struct Enumeration {
    const Problem& P;
    const std::vector<int>& vars;  // the variables to enumerate, in search order
    std::uint64_t budget;
    std::vector<int> sa, sb, rem, nz;  // per-constraint running state
    std::vector<int> val;
    std::vector<std::array<std::uint64_t, 5>> cnt;  // variable x value -> number of solutions
    std::uint64_t total = 0;
    std::uint64_t nodes = 0;
    bool aborted = false;

    Enumeration(const Problem& p, const std::vector<int>& v, std::uint64_t b)
        : P(p), vars(v), budget(b), sa(p.cons.size(), 0), sb(p.cons.size(), 0), rem(p.cons.size(), 0),
          nz(p.cons.size(), 0), val(p.cell.size(), 0), cnt(p.cell.size()) {
        for (std::size_t i = 0; i < p.cons.size(); ++i) rem[i] = p.cons[i].n;
    }

    void dfs(std::size_t k) {
        if (k == vars.size()) {
            ++total;
            for (int v : vars) ++cnt[v][val[v]];
            return;
        }
        const int v = vars[k];
        for (int t = 0; t <= 4 && !aborted; ++t) {
            if (((P.dom[v] >> t) & 1) == 0) continue;
            if (++nodes > budget) {
                aborted = true;
                return;
            }
            const int da = t ? TYPES[t - 1][0] : 0;
            const int db = t ? TYPES[t - 1][1] : 0;
            for (int ci : P.cons_of[v]) {
                sa[ci] += da;
                sb[ci] += db;
                --rem[ci];
                nz[ci] += t != 0;
            }
            bool ok = true;
            for (int ci : P.cons_of[v]) {
                const Con& c = P.cons[ci];
                if (!reachable(P.mode, rem[ci], sa[ci], sb[ci]).test(static_cast<std::size_t>(c.target + 64)) ||
                    (rem[ci] == 0 && c.need_mine && nz[ci] == 0)) {
                    ok = false;
                    break;
                }
            }
            if (ok) {
                val[v] = t;
                dfs(k + 1);
            }
            for (int ci : P.cons_of[v]) {
                sa[ci] -= da;
                sb[ci] -= db;
                ++rem[ci];
                nz[ci] -= t != 0;
            }
        }
    }

    bool complete() const { return !aborted && total > 0; }
};

// What the enumerations have learned, indexed by variable of the full problem.
struct Findings {
    std::vector<char> safe, mine;
    std::vector<char> known_mine;  // already marked: finding "this is a mine" again is not news
    std::vector<float> prob;       // exact mine probability from a complete enumeration, or -1
    bool any = false;

    explicit Findings(std::size_t n) : safe(n, 0), mine(n, 0), known_mine(n, 0), prob(n, -1.0f) {}

    void add(int v, std::uint64_t cnt0, std::uint64_t total) {
        if (cnt0 == total) {
            safe[v] = 1;
            any = true;
        } else if (cnt0 == 0 && !known_mine[v]) {
            mine[v] = 1;
            any = true;
        }
    }
};

// Variables connected through shared constraints. group_of[v] = index of v's component.
std::vector<std::vector<int>> components(const Problem& P, std::vector<int>& group_of) {
    std::vector<int> parent(P.cell.size());
    std::iota(parent.begin(), parent.end(), 0);
    const auto find = [&](int x) {
        while (parent[x] != x) x = parent[x] = parent[parent[x]];
        return x;
    };
    for (const Con& c : P.cons) {
        for (int i = 1; i < c.n; ++i) parent[find(c.vars[i])] = find(c.vars[0]);
    }
    std::vector<std::vector<int>> groups;
    std::vector<int> id_of_root(P.cell.size(), -1);
    group_of.assign(P.cell.size(), -1);
    for (int v = 0; v < static_cast<int>(P.cell.size()); ++v) {
        const int r = find(v);
        if (id_of_root[r] < 0) {
            id_of_root[r] = static_cast<int>(groups.size());
            groups.emplace_back();
        }
        group_of[v] = id_of_root[r];
        groups[id_of_root[r]].push_back(v);
    }
    return groups;
}

// Constraints within `radius` steps of constraint `center` (two constraints are one step apart
// if they share a variable).
std::vector<int> window(const Problem& P, int center, int radius) {
    std::vector<char> in(P.cons.size(), 0);
    std::vector<int> ids{center};
    in[center] = 1;
    for (int step = 0, begin = 0; step < radius; ++step) {
        const int end = static_cast<int>(ids.size());
        for (int i = begin; i < end; ++i) {
            const Con& c = P.cons[ids[i]];
            for (int k = 0; k < c.n; ++k) {
                for (int cj : P.cons_of[c.vars[k]]) {
                    if (!in[cj]) {
                        in[cj] = 1;
                        ids.push_back(cj);
                    }
                }
            }
        }
        begin = end;
    }
    return ids;
}

}  // namespace

std::size_t Solver::markCount() const {
    return static_cast<std::size_t>(std::count_if(marks_.begin(), marks_.end(), [](auto m) { return m != 0; }));
}

bool Solver::analyze(const Game& g) {
    Problem P = build(g, marks_);
    if (!propagate(P)) return false;  // contradictory information (should not happen)
    const int nv = static_cast<int>(P.cell.size());

    // Level 1: whatever constraint propagation already decided.
    Findings F(static_cast<std::size_t>(nv));
    for (int v = 0; v < nv; ++v) F.known_mine[v] = marks_[P.cell[v]] != 0;
    bool decided = false;
    for (int v = 0; v < nv; ++v) {
        if (P.dom[v] == 1) {
            F.safe[v] = 1;
            decided = true;
        } else if ((P.dom[v] & 1) == 0 && marks_[P.cell[v]] == 0) {
            // Only a mine that is not marked yet counts; a marked cell's domain already excludes 0.
            F.mine[v] = 1;
            decided = true;
        }
    }

    // Level 2: exhaustive enumeration, which also yields mine probabilities for guessing.
    if (!decided) {
        std::vector<int> group_of;
        const auto comps = components(P, group_of);
        std::vector<int> too_big;
        for (std::size_t ci = 0; ci < comps.size(); ++ci) {
            Enumeration e(P, comps[ci], kComponentBudget);
            e.dfs(0);
            if (e.aborted) {
                too_big.push_back(static_cast<int>(ci));
            } else if (e.total > 0) {
                for (int v : comps[ci]) {
                    F.add(v, e.cnt[v][0], e.total);
                    F.prob[v] = 1.0f - static_cast<float>(e.cnt[v][0]) / static_cast<float>(e.total);
                }
            }
        }
        // Components too big for the budget: enumerate small windows around each constraint
        // instead, widening the window until something is found. A window is a relaxation, so
        // what it proves is certain, but its probabilities are not trustworthy and are not used.
        if (!F.any && !too_big.empty()) {
            std::vector<std::vector<int>> cons_of_comp(comps.size());
            for (std::size_t ci = 0; ci < P.cons.size(); ++ci) {
                cons_of_comp[group_of[P.cons[ci].vars[0]]].push_back(static_cast<int>(ci));
            }
            for (int radius = 1; radius <= kMaxWindowRadius && !F.any; ++radius) {
                for (int comp : too_big) {
                    for (int center : cons_of_comp[comp]) {
                        std::vector<int> back;
                        const Problem Q = subProblem(P, window(P, center, radius), back);
                        std::vector<int> all(back.size());
                        std::iota(all.begin(), all.end(), 0);
                        Enumeration e(Q, all, kWindowBudget);
                        e.dfs(0);
                        if (!e.complete()) continue;
                        for (std::size_t i = 0; i < back.size(); ++i) F.add(back[i], e.cnt[i][0], e.total);
                    }
                }
            }
        }
    }

    if (F.any || decided) {
        // Open the safe cells first (that makes progress), then mark mines. queue_ is consumed
        // from the back, so insert in reverse.
        std::vector<Move> opens, mines;
        for (int v = 0; v < nv; ++v) {
            if (F.safe[v] && !F.mine[v]) {
                Move m;
                m.kind = MoveKind::open;
                m.cell = P.cell[v];
                opens.push_back(m);
            } else if (F.mine[v] && !F.safe[v] && marks_[P.cell[v]] == 0) {
                Move m;
                m.kind = MoveKind::mark;
                m.cell = P.cell[v];
                mines.push_back(m);
            }
        }
        if (!opens.empty() || !mines.empty()) {
            queue_.clear();
            queue_.insert(queue_.end(), mines.rbegin(), mines.rend());
            queue_.insert(queue_.end(), opens.rbegin(), opens.rend());
            return true;
        }
    }

    // Level 3: guess. Frontier cells with an exact probability use it. Everything else (cells
    // away from the frontier and frontier cells of components that were too big) is estimated
    // with the same density: (mines not accounted for) / (cells not accounted for).
    double exact_mines = 0.0;
    std::size_t other_count = 0;
    int best_v = -1;
    for (int v = 0; v < nv; ++v) {
        if (marks_[P.cell[v]] != 0) continue;
        if (F.prob[v] < 0) {
            ++other_count;
            continue;
        }
        exact_mines += F.prob[v];
        if (best_v < 0 || F.prob[v] < F.prob[best_v]) best_v = v;
    }
    std::size_t outside_best = g.n;
    std::size_t outside_best_nbrs = 99;
    for (std::size_t i = 0; i < g.n; ++i) {
        if (g.open[i] != 0 || marks_[i] != 0 || P.var_of[i] >= 0) continue;
        ++other_count;
        // Fewer neighbors = more like a corner or edge, which is likelier to open a blank area.
        const std::size_t nb = g.nbrs(i).size();
        if (nb < outside_best_nbrs) {
            outside_best_nbrs = nb;
            outside_best = i;
        }
    }
    const double mines_left =
        std::max(0.0, static_cast<double>(g.mines) - static_cast<double>(markCount()) - exact_mines);
    const double density = other_count ? std::min(1.0, mines_left / static_cast<double>(other_count)) : 2.0;

    Move m;
    m.kind = MoveKind::open;
    m.reason = Reason::guess;
    if (best_v >= 0 && static_cast<double>(F.prob[best_v]) <= density) {
        m.cell = P.cell[best_v];
        m.risk = F.prob[best_v];
    } else if (outside_best < g.n) {
        m.cell = static_cast<std::uint16_t>(outside_best);
        m.risk = static_cast<float>(density);
    } else {
        // Only frontier cells of oversized components are left: take the first unmarked one.
        int any = -1;
        for (int v = 0; v < nv && any < 0; ++v) {
            if (marks_[P.cell[v]] == 0 && F.prob[v] < 0) any = v;
        }
        if (any < 0) return false;
        m.cell = P.cell[any];
        m.risk = static_cast<float>(density);
    }
    queue_.assign(1, m);
    return true;
}

Move Solver::step(Game& g, std::uint32_t now_ms) {
    if (g.over) return {};
    if (!g.started) {
        queue_.clear();
        marks_.fill(0);
        Move m;
        m.kind = MoveKind::open;
        m.reason = Reason::first_click;
        m.cell = static_cast<std::uint16_t>((static_cast<std::size_t>(g.h) / 2) * g.w + g.w / 2);
        g.startAt(m.cell, now_ms);
        return m;
    }
    for (int attempt = 0; attempt < 2; ++attempt) {
        while (!queue_.empty()) {
            const Move m = queue_.back();
            queue_.pop_back();
            if (g.open[m.cell] != 0 || marks_[m.cell] != 0) continue;  // already handled
            if (m.kind == MoveKind::open) {
                if (g.flag[m.cell] != 0) g.setFlag(m.cell, 0);  // a flag the player placed must not block the solver
                g.reveal(m.cell);
            } else {
                marks_[m.cell] = 1;
            }
            return m;
        }
        if (!analyze(g)) return {};
    }
    return {};
}

}  // namespace cw
