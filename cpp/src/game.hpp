// 复扫雷 · 規則與狀態（純邏輯，不含任何 GUI）
//
// 資料欄位刻意保持 public：呼叫端（介面、測試）可以直接讀 mine / clue / open / flag。
// 會改動局面的函式都會檢查格子編號，越界一律視為無操作。
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

namespace cw {

inline constexpr std::size_t MAX_W = 40;
inline constexpr std::size_t MAX_H = 30;
inline constexpr std::size_t MAX_CELLS = MAX_W * MAX_H;
inline constexpr std::size_t MAX_MINES = 999;
static_assert(MAX_CELLS <= UINT16_MAX, "格子編號以 uint16_t 儲存");

// 四種雷：(實部, 虛部)。兩個模式共用這四種雷，只換單位：
//   圓複數模式       i² = −1：+1、−1、+i、−i，顯示值 a² + b²（恆非負）
//   閔可夫斯基模式   j² = +1：+1、−1、+j、−j，顯示值 a² − b²（可負）
inline constexpr std::array<std::array<int, 2>, 4> TYPES{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

// 玩法模式
enum class Mode : std::uint8_t { complex = 0, hyper = 1 };

// 圓複數模式的 24 個可能顯示值
inline constexpr std::array<std::uint16_t, 24> ACHIEVABLE{
    0, 1, 2, 4, 5, 8, 9, 10, 13, 16, 17, 18, 20, 25, 26, 29, 32, 34, 36, 37, 40, 49, 50, 64};

struct Preset {
    std::uint16_t w;
    std::uint16_t h;
    std::uint16_t mines;
    std::string_view label;
};

// 標準三檔
inline constexpr std::array<Preset, 3> PRESETS{{
    {9, 9, 10, "初級 9×9 · 10 雷"},
    {16, 16, 40, "中級 16×16 · 40 雷"},
    {30, 16, 99, "高級 30×16 · 99 雷"},
}};

// 上一次操作的回饋（文案在介面層，這裡只給代號）
enum class Msg : std::uint8_t {
    none = 0,
    started,
    judge_fail,
    expand_ok,  // msg_arg = 這次展開所涉及的鄰格數
    win,
    lose,
};

// mulberry32：同種子產生同一串亂數，因此同種子同開局格得到同一個棋盤
class Rng {
public:
    explicit Rng(std::uint32_t seed = 1) : a_(seed == 0 ? 1 : seed) {}

    // 回傳 [0, 1)
    [[nodiscard]] double next();
    // 回傳 [0, n)；n == 0 時回傳 0
    [[nodiscard]] std::size_t below(std::size_t n);

private:
    std::uint32_t a_;
};

// 把總雷數盡量均分給四種雷（下標 1..4，下標 0 不用）
[[nodiscard]] std::array<std::uint16_t, 5> splitEvenly(std::uint16_t total);

// 一個格子的 8 鄰域（邊界外的不算）。可直接 range-for。
struct Nbrs {
    std::array<std::uint16_t, 8> cells{};
    std::uint8_t count = 0;

    const std::uint16_t* begin() const { return cells.data(); }
    const std::uint16_t* end() const { return cells.data() + count; }
    std::size_t size() const { return count; }
    std::span<const std::uint16_t> span() const { return {cells.data(), count}; }
};

class Game {
public:
    Game();

    std::uint16_t w = 9;
    std::uint16_t h = 9;
    // 格子總數。只由 newGame() 依 w、h 重算，所以改了 w / h 之後要先呼叫 newGame()
    std::size_t n = 81;
    // 當前玩法模式（newGame 不動它：模式是設定，不是局面的一部分）
    Mode mode = Mode::complex;
    // 閔可夫斯基模式的判據取「備選」（較寬）那一檔：只要求 a²−b² 與旗數相同
    bool judge_loose = false;

    std::array<std::uint8_t, MAX_CELLS> mine{};  // 0 = 無雷，1..4 = 雷的種類
    // 顯示值：圓複數模式存 a²+b²（0…64），閔可夫斯基模式存 a²−b²（−64…64）；雷格與未計算為 −1
    std::array<std::int16_t, MAX_CELLS> clue{};
    std::array<std::uint8_t, MAX_CELLS> open{};
    std::array<std::uint8_t, MAX_CELLS> flag{};  // 0 = 無旗，1..4 = 旗的種類

    std::uint32_t seed = 1;
    Rng rng{1};
    std::uint16_t mines = 10;
    // 各類雷總數（下標 1..4），開局公開
    std::array<std::uint16_t, 5> type_total{};
    std::array<std::uint16_t, 5> flags_of{};
    // 自定義配比（下標 1..4）；全 0 = 類型隨機撒
    std::array<std::uint16_t, 5> type_count{};

    bool started = false;
    bool over = false;
    bool win = false;
    std::int32_t boom = -1;
    std::int32_t start_cell = -1;
    std::uint32_t elapsed_ms = 0;
    std::uint32_t t0 = 0;
    std::uint32_t moves = 0;
    Msg msg = Msg::none;
    std::uint16_t msg_arg = 0;

    [[nodiscard]] std::size_t cellCount() const { return n; }
    [[nodiscard]] bool inb(int r, int c) const;

    [[nodiscard]] Nbrs nbrs(std::size_t cell) const;
    [[nodiscard]] std::size_t nbrMineCount(std::size_t cell) const;
    // 鄰域裡的旗幟數
    [[nodiscard]] std::size_t nbrFlagCount(std::size_t cell) const;
    // 空白格：自己無雷、鄰域也無雷；只有它會連片展開
    [[nodiscard]] bool isBlank(std::size_t cell) const;
    [[nodiscard]] std::size_t flagsTotal() const;
    // 該類雷還剩幾顆沒標（可為負）
    [[nodiscard]] std::int32_t unmarked(std::size_t t) const;

    void setMsg(Msg m, std::uint16_t arg = 0);
    void setSeed(std::uint32_t s);

    // 新開一局（棋盤等第一次點擊再生成）。w、h 會被夾進 [1, MAX]，mines 夾進 [0, MAX_MINES]
    void newGame(std::uint32_t new_seed);
    // 第一次點擊：重置亂數、布雷、計算顯示值，並從開局格連片
    void startAt(std::size_t cell, std::uint32_t now_ms);
    // 布雷 + 算顯示值 + 從開局格連片。開局格與它的鄰域保證無雷
    void genBoard(std::size_t start);
    void computeClues();
    void countTypes();

    // 鄰域按類型統計的 (a, b)：a = 正實 − 負實，b = 正單位雷 − 負單位雷。
    // use_flag 為真時統計旗幟，否則統計真雷。
    [[nodiscard]] std::array<int, 2> sumsOf(std::size_t cell, bool use_flag) const;

    // 連片翻開：只在空白格上擴散，回傳新翻開的格數；插了旗的格子會被繞開
    std::size_t cascadeOpen(std::span<const std::uint16_t> seeds);

    // 插 / 改 / 清旗（不限量）。t 必須在 0..4，否則回傳 false
    bool setFlag(std::size_t cell, std::uint8_t t);
    // 右鍵循環：空 → +1 → −1 → +i → −i → 空
    bool cycleFlag(std::size_t cell);
    // 翻開一格；插了旗的翻不開
    void reveal(std::size_t cell);

    // 判據：旗幟數 = 鄰域真實雷數，且「顯示值區分不出來的差別」允許存在。
    //   圓複數模式：允許實虛比例等於真值比例或其倒數。
    //   閔可夫斯基模式：要求 |a|、|b| 分別相符；judge_loose 時只要求 a²−b² 相同。
    [[nodiscard]] bool matchComboTruth(std::size_t cell) const;
    // 展開：判據過了就翻開周圍未插旗的格；踩到雷則判負
    void tryExpand(std::size_t cell);
    void checkWin();
    void lose(std::size_t cell);

    [[nodiscard]] std::size_t openedCount() const;
    [[nodiscard]] std::size_t safeCount() const;
    [[nodiscard]] std::size_t correctFlags() const;
    [[nodiscard]] std::size_t typeSum() const;

private:
    [[nodiscard]] bool validCell(std::size_t cell) const { return cell < n; }
};

}  // namespace cw
