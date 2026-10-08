// 規則自檢：跑規則層不變量（無界面）
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
        std::printf("  [失敗] %s\n", name);
    }
}

using Flags = std::array<bool, MAX_CELLS>;
using Counts = std::array<std::uint16_t, 5>;
constexpr Counts kRandom{};  // 全 0 = 類型隨機撒

// 獨立的洪水填充，用來核對連片：連片覆蓋 = 連通空白格 ∪ 它們的非雷鄰居
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

// 閔可夫斯基自檢用：9×9 棋盤的格子 12 周圍正好 8 個鄰居，按「四種雷的顆數」配比擺上去。
// as_flag = true 時擺的是旗幟（不重算顯示值），否則擺真雷並重算。
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

// ---- 1. 隨機數確定性 ----
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
    expect(same, "同種子 + 同開局格應生成完全相同的棋盤");
    expect(diff, "不同種子應生成不同棋盤");

    Rng r1(42), r2(42);
    bool in_range = true;
    for (int i = 0; i < 1000; ++i) {
        const double x = r1.next();
        in_range = in_range && x == r2.next() && x >= 0.0 && x < 1.0;
    }
    expect(in_range, "Rng 同種子同序列，且落在 [0, 1)");
    Rng r3(7);
    bool below_ok = true;
    for (int i = 0; i < 1000; ++i) below_ok = below_ok && r3.below(10) < 10;
    expect(below_ok && r3.below(0) == 0, "below(n) < n，below(0) = 0");
    Rng z0(0), z1(1);
    expect(z0.next() == z1.next(), "種子 0 與種子 1 等價");

    bool split_ok = splitEvenly(10) == Counts{0, 3, 3, 2, 2};
    for (std::uint16_t total = 0; total <= MAX_MINES; ++total) {
        const Counts s = splitEvenly(total);
        split_ok = split_ok && s[1] + s[2] + s[3] + s[4] == total && s[1] - s[4] <= 1;
    }
    expect(split_ok, "splitEvenly 總和守恆且各類相差不超過 1");
    std::printf("1 隨機數確定性：完成\n");
}

// ---- 2. 精確配比 ----
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
    expect(bad == 0, "指定配比必須被精確執行（5 種配比 × 3 種子）");
    std::printf("2 精確配比：%s\n", bad == 0 ? "通過" : "有偏差");
}

// ---- 3. 純實 / 純虛局面的顯示值全是完全平方數 ----
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
    expect(bad == 0, "純實/純虛局面裡所有顯示值都必須是完全平方數");
    std::printf("3 純實/純虛不變量：%s\n", bad == 0 ? "通過" : "失敗");
}

// ---- 4. 連片：與獨立洪水填充逐格一致，且絕不翻雷 ----
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
        // 顯示 0 但周圍有雷（抵消對）的格子絕不連片
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
    expect(samples >= 20, "空白格連片樣本數足夠");
    expect(mismatch == 0, "連片結果必須與獨立洪水填充逐格一致");
    expect(mine_opened == 0, "連片絕不能翻開雷");
    expect(zero_samples >= 5, "顯示 0 的樣本數足夠");
    expect(zero_cascaded == 0, "顯示 0 的格子絕不能連片");
    std::printf("4 連片展開：空白樣本 %d，顯示 0 樣本 %d，不一致 %d，翻雷 %d\n", samples, zero_samples,
                mismatch, mine_opened);
}

// ---- 4b. 大盤開局連片 ----
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
    expect(samples >= 72, "大盤連片樣本數足夠（6 種盤面 × 6 種子 × 4 開局格）");
    expect(big >= 8, "大盤樣本裡應有真正的大連片（≥400 格）");
    expect(mismatch == 0, "大盤開局連片必須與獨立洪水填充逐格一致");
    expect(opened_mine == 0, "大盤連片也絕不能翻開雷");
    std::printf("4b 大盤連片：樣本 %d，大連片 %d 次，一次最多翻開 %zu 格，逐格不一致 %d\n", samples, big, worst,
                mismatch);
}

// ---- 5. 開局必定連片且不踩雷 ----
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
    expect(bad == 0, "開局格必為空白格、必連片（≥9 格）且不踩雷；各類雷數已知");
    std::printf("5 開局連片與分類計數：%s\n", bad == 0 ? "通過" : "失敗");
}

// ---- 6. 判據（圓複數模式）：與獨立實作一致 ----
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
    expect(total > 50, "判據樣本數足夠");
    expect(mismatch == 0, "判據結果必須與獨立實作一致");
    expect(pass > 0, "判據應至少放行一部分組合");
    std::printf("6 組合匹配判據：樣本 %d，放行 %d，不一致 %d\n", total, pass, mismatch);
}

// ---- 7. 插旗不限量 + 循環順序 ----
void testFlags() {
    Game game;
    buildBoard(game, 9, 9, 10, kRandom, 701, 40);
    unsigned placed = 0;
    for (std::size_t i = 0; i < game.n; ++i) {
        if (game.open[i] == 0 && game.setFlag(i, 1)) ++placed;
    }
    expect(placed > 0, "所有未翻開格都能插旗");
    expect(game.flags_of[1] == placed, "計數與實際插旗數一致");
    expect(game.unmarked(1) < 0, "插超後未標記數應為負數");
    expect(game.flagsTotal() == placed, "flagsTotal 與實際插旗數一致");

    std::size_t cell = 0;
    while (game.open[cell] != 0) ++cell;
    game.setFlag(cell, 0);
    std::array<std::uint8_t, 6> seq{};
    for (auto& s : seq) {
        game.cycleFlag(cell);
        s = game.flag[cell];
    }
    expect((seq == std::array<std::uint8_t, 6>{1, 2, 3, 4, 0, 1}), "右鍵循環必須是 1,2,3,4,0,1");
    std::printf("7 插旗不限量：插了 %u 面\n", placed);
}

// ---- 8. 旗子保護格子 ----
void testFlagProtection() {
    Game game;
    buildBoard(game, 9, 9, 10, kRandom, 801, 40);
    std::size_t cell = 0;
    while (!(game.open[cell] == 0 && game.mine[cell] == 0)) ++cell;
    game.setFlag(cell, 3);
    expect(game.flags_of[3] == 1, "插旗後計數為 1");
    game.reveal(cell);
    expect(game.open[cell] == 0, "插旗的格子翻不開");
    expect(game.flag[cell] == 3, "翻不開時旗幟應原樣保留");
    expect(game.flags_of[3] == 1, "翻不開時計數不動");

    // 連片也不該把旗子吃掉
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
        expect(game.open[blank] == 1, "空白格應能翻開");
        expect(game.open[flagged_nbr] == 0, "連片展開不該翻開插了旗的格子");
        expect(game.flag[flagged_nbr] == 1, "連片展開不該清掉旗子");
    }
    game.setFlag(cell, 0);
    game.reveal(cell);
    expect(game.open[cell] == 1, "撤旗後應能翻開");
    expect(game.flags_of[3] == 0, "撤旗後計數歸還");
    std::printf("8 旗子保護格子（翻不開、連片也不碰）：完成\n");
}

// ---- 9. 勝負判定 ----
void testWinLose() {
    Game game;
    buildBoard(game, 9, 9, 10, kRandom, 901, 40);
    for (std::size_t i = 0; i < game.n; ++i) {
        if (game.mine[i] == 0 && game.open[i] == 0) game.reveal(i);
    }
    expect(game.win && game.over, "翻開所有非雷格必須判勝");
    expect(game.msg == Msg::win, "勝利時回饋為 win");
    expect(game.openedCount() == game.safeCount(), "勝利時已翻開格數 = 非雷格數");

    buildBoard(game, 9, 9, 10, kRandom, 902, 40);
    std::size_t left = 0;
    while (!(game.mine[left] == 0 && game.open[left] == 0)) ++left;
    for (std::size_t i = 0; i < game.n; ++i) {
        if (i != left && game.mine[i] == 0 && game.open[i] == 0) game.reveal(i);
    }
    expect(!game.win, "還剩非雷格未翻開時不能判勝");

    buildBoard(game, 9, 9, 10, kRandom, 903, 40);
    std::size_t m = 0;
    while (game.mine[m] == 0) ++m;
    game.reveal(m);
    expect(game.over && !game.win, "翻開雷必須判負");
    expect(game.boom == static_cast<std::int32_t>(m), "記錄踩中的格子");
    expect(game.msg == Msg::lose, "失敗時回饋為 lose");
    game.reveal(0);  // 結束後再操作不得改變局面
    expect(game.boom == static_cast<std::int32_t>(m), "結束後的操作無效");
    std::printf("9 勝負判定：完成\n");
}

// ---- 10. 展開：判據不過時棋盤不變 ----
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
        expect(openSnapshot(game) == before, "判據不通過時展開不能改變棋盤");
        expect(game.msg == Msg::judge_fail, "判據不通過時回饋為 judge_fail");
    }
    std::printf("10 展開門禁：完成\n");
}

// ---- 11. 閔可夫斯基模式 ----
void testHyper() {
    // 枚舉 495 種鄰域組合（四種雷的顆數 n1..n4，總數 ≤ 8）：兩套顯示值集合都算一遍
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
    expect(combos == 495, "鄰域組合應枚舉出 495 種");
    const auto cn = std::count(seen_c.begin(), seen_c.end(), true);
    const auto hn = std::count(seen_h.begin(), seen_h.end(), true);
    expect(cn == static_cast<long>(ACHIEVABLE.size()), "圓複數模式的顯示值應為 24 個");
    expect(hn == 39, "閔可夫斯基模式的顯示值應為 39 個");
    bool cplx_ok = true;
    for (auto D : ACHIEVABLE) cplx_ok = cplx_ok && seen_c[D];
    expect(cplx_ok, "圓複數模式的顯示值集合必須正好是 ACHIEVABLE 那 24 個");
    const int MAG[] = {1, 3, 4, 5, 7, 8, 9, 12, 15, 16, 21, 24, 25, 32, 35, 36, 48, 49, 64};
    bool pair_ok = seen_h[64];
    for (int m : MAG) pair_ok = pair_ok && seen_h[m + 64] && seen_h[-m + 64];
    expect(pair_ok, "閔可夫斯基模式應是 19 個模長各帶正負、外加一個 0");
    std::printf("11 顯示值集合：組合 %d 種，圓複數 %ld 值，閔可夫斯基 %ld 值\n", combos, static_cast<long>(cn),
                static_cast<long>(hn));

    // 顯示值算法：a² − b²（圓複數模式是 a² + b²），負值照算
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
    expect(clue_bad == 0, "閔可夫斯基顯示值必須是 a² − b²（12 組配比逐一核對）");
    h.mode = Mode::complex;
    hyperPlace(h, {2, 1, 3, 1}, false);
    expect(h.clue[12] == 5, "同一配比在圓複數模式下應是 a² + b²（1+4=5）");
    h.mode = Mode::hyper;

    // 判據：真值 = 一顆 +1 加一顆 +j（a=1、b=1、共 2 顆，顯示值 0）
    hyperPlace(h, {1, 0, 1, 0}, false);
    expect(h.clue[12] == 0, "真值配比的顯示值應為 0");
    hyperPlace(h, {1, 0, 1, 0}, true);
    expect(h.matchComboTruth(12), "旗幟與真值一致時判據應通過");
    hyperPlace(h, {0, 1, 0, 1}, true);
    expect(h.matchComboTruth(12), "推薦判據允許 a、b 各自取負（四種符號組合）");
    hyperPlace(h, {1, 1, 0, 0}, true);
    expect(!h.matchComboTruth(12), "推薦判據：|a|、|b| 不相符必須擋住");
    h.judge_loose = true;
    expect(h.matchComboTruth(12), "備選判據：同旗數且 a²−b² 相同就應通過");
    h.judge_loose = false;
    hyperPlace(h, {1, 0, 0, 0}, true);
    expect(!h.matchComboTruth(12), "旗數與真實雷數不符必須擋住（兩種判據都一樣）");
    hyperPlace(h, {2, 0, 0, 0}, false);
    hyperPlace(h, {1, 1, 0, 0}, true);
    expect(!h.matchComboTruth(12), "旗數 2 = 2 但 |a| 不同（2 與 0）仍須擋住");

    // 展開：判據過了就翻開其餘未插旗的鄰格
    hyperPlace(h, {1, 0, 1, 0}, false);
    hyperPlace(h, {1, 0, 1, 0}, true);
    h.setFlag(80, 1);  // 遠處的格子插旗：連片繞開它，所以這一步展開不會把整盤翻完而判勝
    h.open[12] = 1;
    h.over = h.win = false;
    h.boom = -1;
    h.setMsg(Msg::none);
    h.tryExpand(12);
    expect(h.msg == Msg::expand_ok && !h.win, "閔可夫斯基模式判據通過後展開應回饋 expand_ok");
    expect(h.msg_arg == 6, "展開回饋帶著涉及的鄰格數（8 鄰格 − 2 面旗 = 6）");
    expect(h.boom < 0, "鄰域裡的雷都插了旗，展開不該踩雷");
    bool opened_any = false;
    for (std::size_t i = 0; i < h.n; ++i) opened_any = opened_any || (i != 12 && h.open[i] != 0);
    expect(opened_any, "判據通過後應真的翻開鄰格");

    // 旗少插一面卻判據通過是不可能的；旗插錯位置則踩雷
    hyperPlace(h, {1, 0, 1, 0}, false);
    hyperPlace(h, {0, 0, 0, 0}, true);
    const Nbrs nb = h.nbrs(12);
    std::size_t wrong1 = nb.cells[6], wrong2 = nb.cells[7];  // 雷在 slot 0、1，旗插到別處
    h.setFlag(wrong1, 1);
    h.setFlag(wrong2, 3);
    h.open[12] = 1;
    h.over = h.win = false;
    h.tryExpand(12);
    expect(h.over && !h.win && h.boom >= 0, "旗插錯位置、判據卻通過時，展開踩雷判負");
    std::printf("11b/11c 閔可夫斯基顯示值算法與判據：完成\n");
}

// ---- 12. 健壯性：越界與極端參數 ----
void testRobustness() {
    Game g;
    buildBoard(g, 9, 9, 10, kRandom, 1201, 40);
    const Flags before = openSnapshot(g);
    g.reveal(10000);
    g.tryExpand(10000);
    expect(!g.cycleFlag(10000), "cycleFlag 越界回傳 false");
    expect(!g.setFlag(10000, 1), "setFlag 越界回傳 false");
    expect(!g.setFlag(0, 5), "setFlag 種類 > 4 回傳 false");
    expect(!g.isBlank(10000) && g.nbrs(10000).size() == 0, "越界格子沒有鄰域、也不是空白格");
    expect(openSnapshot(g) == before && !g.over, "越界操作不改變局面");
    const auto seed_before = g.seed;
    g.startAt(10000, 0);
    expect(g.seed == seed_before && g.started, "startAt 越界是無操作");

    // 超量的自訂配比：不崩潰、不超過可用格數
    Game big;
    big.w = 40;
    big.h = 30;
    big.type_count = {0, 999, 999, 999, 999};
    big.newGame(5);
    big.startAt(0, 0);
    expect(big.mines <= big.n - 4 && big.mines == big.typeSum(), "超量配比被截到可用格數");
    expect(big.mine[0] == 0 && big.mine[1] == 0 && big.mine[40] == 0 && big.mine[41] == 0, "超量配比仍保留開局安全區");

    // 尺寸與雷數夾限
    Game c;
    c.w = 0;
    c.h = 500;
    c.mines = 60000;
    c.newGame(1);
    expect(c.w == 1 && c.h == MAX_H && c.n == MAX_H, "w、h 被夾進合法範圍");
    expect(c.mines == MAX_MINES, "mines 被夾進 MAX_MINES");

    // 最小盤面：1×1 沒有鄰居，翻開即勝
    Game one;
    one.w = 1;
    one.h = 1;
    one.mines = 0;
    one.newGame(1);
    one.startAt(0, 0);
    expect(one.over && one.win, "1×1 無雷棋盤開局即勝");

    // 3×3 且開局在中央：安全區吃掉整盤，一顆雷也放不下，開局即勝
    Game tiny;
    tiny.w = 3;
    tiny.h = 3;
    tiny.mines = 5;
    tiny.newGame(1);
    tiny.startAt(4, 0);
    expect(tiny.mines == 0 && tiny.over && tiny.win, "安全區吃滿整盤時雷數為 0 並直接勝利");
    std::printf("12 健壯性：完成\n");
}

}  // namespace

int main() {
    std::printf("复扫雷 · 規則自檢（C++）\n==========================\n");
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

    std::printf("\n斷言 %d 項，失敗 %d 項\n%s\n", g_checks, g_failed, g_failed == 0 ? "全部通過" : "存在失敗");
    return g_failed == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
