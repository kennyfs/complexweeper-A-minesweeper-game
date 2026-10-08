// 复扫雷 · 規則與狀態的實作
#include "game.hpp"

#include <algorithm>
#include <cstdlib>

namespace cw {

// ------------------------------------------------------------------ Rng

double Rng::next() {
    a_ += 0x6D2B79F5u;
    std::uint32_t t = a_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + ((t ^ (t >> 7)) * (t | 61u));
    return static_cast<double>(t ^ (t >> 14)) / 4294967296.0;
}

std::size_t Rng::below(std::size_t n) {
    if (n == 0) return 0;
    return static_cast<std::size_t>(next() * static_cast<double>(n));
}

std::array<std::uint16_t, 5> splitEvenly(std::uint16_t total) {
    std::array<std::uint16_t, 5> out{};
    const auto base = static_cast<std::uint16_t>(total / 4);
    auto rest = static_cast<std::uint16_t>(total - base * 4);
    for (std::size_t t = 1; t <= 4; ++t) {
        out[t] = base;
        if (rest > 0) {
            ++out[t];
            --rest;
        }
    }
    return out;
}

// ------------------------------------------------------------------ 鄰域與基本查詢

Game::Game() {
    clue.fill(-1);
}

bool Game::inb(int r, int c) const {
    return r >= 0 && c >= 0 && r < h && c < w;
}

Nbrs Game::nbrs(std::size_t cell) const {
    Nbrs out;
    if (!validCell(cell)) return out;
    const int W = w;
    const int H = h;
    const int r = static_cast<int>(cell) / W;
    const int c = static_cast<int>(cell) % W;
    for (int rr = std::max(r - 1, 0); rr <= std::min(r + 1, H - 1); ++rr) {
        for (int cc = std::max(c - 1, 0); cc <= std::min(c + 1, W - 1); ++cc) {
            if (rr == r && cc == c) continue;
            out.cells[out.count++] = static_cast<std::uint16_t>(rr * W + cc);
        }
    }
    return out;
}

std::size_t Game::nbrMineCount(std::size_t cell) const {
    std::size_t cnt = 0;
    for (std::uint16_t j : nbrs(cell)) cnt += (mine[j] != 0);
    return cnt;
}

std::size_t Game::nbrFlagCount(std::size_t cell) const {
    std::size_t cnt = 0;
    for (std::uint16_t j : nbrs(cell)) cnt += (flag[j] != 0);
    return cnt;
}

bool Game::isBlank(std::size_t cell) const {
    return validCell(cell) && mine[cell] == 0 && nbrMineCount(cell) == 0;
}

std::size_t Game::flagsTotal() const {
    std::size_t s = 0;
    for (std::size_t t = 1; t <= 4; ++t) s += flags_of[t];
    return s;
}

std::int32_t Game::unmarked(std::size_t t) const {
    return static_cast<std::int32_t>(type_total[t]) - static_cast<std::int32_t>(flags_of[t]);
}

std::array<int, 2> Game::sumsOf(std::size_t cell, bool use_flag) const {
    int a = 0;
    int b = 0;
    for (std::uint16_t j : nbrs(cell)) {
        const std::uint8_t t = use_flag ? flag[j] : mine[j];
        if (t == 0) continue;
        a += TYPES[t - 1][0];
        b += TYPES[t - 1][1];
    }
    return {a, b};
}

// ------------------------------------------------------------------ 開局與布雷

void Game::setMsg(Msg m, std::uint16_t arg) {
    msg = m;
    msg_arg = arg;
}

void Game::setSeed(std::uint32_t s) {
    seed = (s == 0) ? 1 : s;
    rng = Rng(seed);
}

void Game::newGame(std::uint32_t new_seed) {
    w = std::clamp<std::uint16_t>(w, 1, MAX_W);
    h = std::clamp<std::uint16_t>(h, 1, MAX_H);
    mines = std::min<std::uint16_t>(mines, MAX_MINES);
    n = static_cast<std::size_t>(w) * h;
    std::fill_n(mine.begin(), n, 0);
    std::fill_n(clue.begin(), n, -1);
    std::fill_n(open.begin(), n, 0);
    std::fill_n(flag.begin(), n, 0);
    started = false;
    over = false;
    win = false;
    boom = -1;
    start_cell = -1;
    elapsed_ms = 0;
    t0 = 0;
    moves = 0;
    type_total.fill(0);
    flags_of.fill(0);
    setSeed(new_seed);
    setMsg(Msg::none);
}

void Game::startAt(std::size_t cell, std::uint32_t now_ms) {
    if (!validCell(cell)) return;
    setSeed(seed);  // 同一局重新開始時，同一個開局格會得到同一個棋盤
    genBoard(cell);
    start_cell = static_cast<std::int32_t>(cell);
    started = true;
    over = false;
    win = false;
    elapsed_ms = 0;
    t0 = now_ms;
    moves = 1;
    setMsg(Msg::none);
    checkWin();  // 開局連片可能剛好翻完所有非雷格（例如極小盤面）
}

void Game::genBoard(std::size_t start) {
    if (!validCell(start)) return;
    std::fill_n(mine.begin(), n, 0);
    std::fill_n(clue.begin(), n, -1);
    std::fill_n(open.begin(), n, 0);

    // 候選位置：除了開局格與它的鄰域之外的所有格子（保證開局格是空白格）
    const int W = w;
    const int H = h;
    const int sr = static_cast<int>(start) / W;
    const int sc = static_cast<int>(start) % W;
    std::array<std::uint16_t, MAX_CELLS> pool;
    std::size_t m = 0;
    for (int r = 0; r < H; ++r) {
        for (int c = 0; c < W; ++c) {
            if (std::abs(r - sr) <= 1 && std::abs(c - sc) <= 1) continue;
            pool[m++] = static_cast<std::uint16_t>(r * W + c);
        }
    }
    // 洗位置
    for (std::size_t i = m; i > 1; --i) {
        std::swap(pool[i - 1], pool[rng.below(i)]);
    }

    std::size_t want = 0;
    for (std::size_t t = 1; t <= 4; ++t) want += type_count[t];
    const std::size_t count = std::min<std::size_t>(want > 0 ? want : mines, m);

    if (want > 0) {
        // 精確配比：先依序鋪出類型序列（最多 count 個），再洗一遍
        std::array<std::uint8_t, MAX_CELLS> list;
        std::size_t ln = 0;
        for (std::size_t t = 1; t <= 4 && ln < count; ++t) {
            for (std::size_t k = 0; k < type_count[t] && ln < count; ++k) {
                list[ln++] = static_cast<std::uint8_t>(t);
            }
        }
        for (std::size_t i = ln; i > 1; --i) {
            std::swap(list[i - 1], list[rng.below(i)]);
        }
        for (std::size_t k = 0; k < ln; ++k) mine[pool[k]] = list[k];
    } else {
        for (std::size_t k = 0; k < count; ++k) {
            mine[pool[k]] = static_cast<std::uint8_t>(1 + rng.below(4));
        }
    }
    mines = static_cast<std::uint16_t>(count);
    computeClues();
    countTypes();
    const std::uint16_t seeds[] = {static_cast<std::uint16_t>(start)};
    cascadeOpen(seeds);
}

void Game::computeClues() {
    for (std::size_t i = 0; i < n; ++i) {
        if (mine[i] != 0) {
            clue[i] = -1;
            continue;
        }
        const auto [a, b] = sumsOf(i, false);
        clue[i] = static_cast<std::int16_t>(mode == Mode::hyper ? a * a - b * b : a * a + b * b);
    }
}

void Game::countTypes() {
    type_total.fill(0);
    for (std::size_t i = 0; i < n; ++i) {
        if (mine[i] != 0) ++type_total[mine[i]];
    }
}

// ------------------------------------------------------------------ 翻開與連片

std::size_t Game::cascadeOpen(std::span<const std::uint16_t> seeds) {
    // 格子在「被發現」的當下就標成已翻開，所以每格最多進棧一次，也不需要另外的查重表。
    // 只有空白格會進棧去擴散它的鄰域；插了旗的格子、雷與已翻開的格子都被繞開。
    std::array<std::uint16_t, MAX_CELLS> stack;
    std::size_t sp = 0;
    std::size_t opened = 0;
    const auto visit = [&](std::size_t i) {
        if (open[i] != 0 || mine[i] != 0 || flag[i] != 0) return;
        open[i] = 1;
        ++opened;
        if (isBlank(i)) stack[sp++] = static_cast<std::uint16_t>(i);
    };
    for (std::uint16_t s : seeds) {
        if (validCell(s)) visit(s);
    }
    while (sp > 0) {
        for (std::uint16_t j : nbrs(stack[--sp])) visit(j);
    }
    return opened;
}

void Game::reveal(std::size_t cell) {
    if (!validCell(cell) || over || open[cell] != 0 || flag[cell] != 0) return;
    open[cell] = 1;
    if (mine[cell] != 0) {
        lose(cell);
        return;
    }
    if (isBlank(cell)) cascadeOpen(nbrs(cell).span());
    ++moves;
    checkWin();
}

// ------------------------------------------------------------------ 旗幟

bool Game::setFlag(std::size_t cell, std::uint8_t t) {
    if (!validCell(cell) || t > 4) return false;
    const std::uint8_t old = flag[cell];
    if (old == t) return true;
    if (old != 0) --flags_of[old];
    flag[cell] = t;
    if (t != 0) ++flags_of[t];
    return true;
}

bool Game::cycleFlag(std::size_t cell) {
    if (!validCell(cell) || over || open[cell] != 0) return false;
    setFlag(cell, static_cast<std::uint8_t>((flag[cell] + 1) % 5));
    ++moves;
    return true;
}

// ------------------------------------------------------------------ 展開判據

bool Game::matchComboTruth(std::size_t cell) const {
    if (!validCell(cell) || nbrMineCount(cell) != nbrFlagCount(cell)) return false;
    if (mode == Mode::hyper) {
        const auto t = sumsOf(cell, false);
        const auto f = sumsOf(cell, true);
        if (judge_loose) {
            return t[0] * t[0] - t[1] * t[1] == f[0] * f[0] - f[1] * f[1];
        }
        return std::abs(f[0]) == std::abs(t[0]) && std::abs(f[1]) == std::abs(t[1]);
    }
    // 圓複數模式：P / V = 真雷中「實類 / 虛類」的顆數，gp / gv = 旗幟中的同樣統計。
    // 顯示值看不出整體取負、也看不出實虛互換，所以實虛配比等於真值或其倒數都算數。
    int P = 0;
    int V = 0;
    int gp = 0;
    int gv = 0;
    for (std::uint16_t j : nbrs(cell)) {
        if (mine[j] != 0) (mine[j] <= 2 ? P : V) += 1;
        if (flag[j] != 0) (flag[j] <= 2 ? gp : gv) += 1;
    }
    return gp + gv == P + V && ((gp == P && gv == V) || (gp == V && gv == P));
}

void Game::tryExpand(std::size_t cell) {
    if (!validCell(cell) || over || open[cell] == 0 || mine[cell] != 0) return;
    std::array<std::uint16_t, 8> uns;
    std::size_t un = 0;
    for (std::uint16_t j : nbrs(cell)) {
        if (open[j] == 0 && flag[j] == 0) uns[un++] = j;
    }
    if (un == 0) return;
    if (!matchComboTruth(cell)) {
        setMsg(Msg::judge_fail);
        return;
    }
    // 判據通過但仍有真雷沒被旗幟蓋住：踩雷
    for (std::uint16_t j : std::span(uns.data(), un)) {
        if (mine[j] != 0) {
            open[j] = 1;
            lose(j);
            return;
        }
    }
    cascadeOpen(std::span<const std::uint16_t>(uns.data(), un));
    ++moves;
    setMsg(Msg::expand_ok, static_cast<std::uint16_t>(un));
    checkWin();
}

// ------------------------------------------------------------------ 勝負與統計

void Game::checkWin() {
    for (std::size_t i = 0; i < n; ++i) {
        if (mine[i] == 0 && open[i] == 0) return;
    }
    over = true;
    win = true;
    setMsg(Msg::win);
}

void Game::lose(std::size_t cell) {
    over = true;
    win = false;
    boom = static_cast<std::int32_t>(cell);
    setMsg(Msg::lose);
}

std::size_t Game::openedCount() const {
    return static_cast<std::size_t>(std::count_if(open.begin(), open.begin() + n, [](auto v) { return v != 0; }));
}

std::size_t Game::safeCount() const {
    return static_cast<std::size_t>(std::count(mine.begin(), mine.begin() + n, 0));
}

std::size_t Game::correctFlags() const {
    std::size_t k = 0;
    for (std::size_t i = 0; i < n; ++i) k += (mine[i] != 0 && flag[i] == mine[i]);
    return k;
}

std::size_t Game::typeSum() const {
    std::size_t s = 0;
    for (std::size_t t = 1; t <= 4; ++t) s += type_total[t];
    return s;
}

}  // namespace cw
