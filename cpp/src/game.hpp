// Complexweeper rules and state (pure logic, no GUI).
//
// The data fields are deliberately public: callers (UI, tests) may read mine / clue / open / flag
// directly. Every function that changes the position checks cell indices; an out-of-range index
// is a no-op.
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
static_assert(MAX_CELLS <= UINT16_MAX, "cell indices are stored as uint16_t");

// The four mine types as (real, imaginary). Both modes share them and only change the unit:
//   complex mode     i^2 = -1: +1, -1, +i, -i; displayed value a^2 + b^2 (never negative)
//   Minkowski mode   j^2 = +1: +1, -1, +j, -j; displayed value a^2 - b^2 (may be negative)
inline constexpr std::array<std::array<int, 2>, 4> TYPES{{{1, 0}, {-1, 0}, {0, 1}, {0, -1}}};

enum class Mode : std::uint8_t { complex = 0, hyper = 1 };

// The 24 displayed values that can occur in complex mode.
inline constexpr std::array<std::uint16_t, 24> ACHIEVABLE{
    0, 1, 2, 4, 5, 8, 9, 10, 13, 16, 17, 18, 20, 25, 26, 29, 32, 34, 36, 37, 40, 49, 50, 64};

struct Preset {
    std::uint16_t w;
    std::uint16_t h;
    std::uint16_t mines;
    std::string_view label;
};

// The three difficulties of each mode. The mine counts are meant to make the solver win about half
// of its games at every size and in both modes (see tools/tune_mines.py). PROVISIONAL: these are
// the values of a quick local run (300 games per size, 95% CI contains 50%); replace them with
// the output of a full tuning run.
inline constexpr std::array<Preset, 3> PRESETS_COMPLEX{{
    {9, 9, 12, "Beginner"},
    {16, 16, 32, "Intermediate"},
    {30, 16, 54, "Expert"},
}};
inline constexpr std::array<Preset, 3> PRESETS_HYPER{{
    {9, 9, 12, "Beginner"},
    {16, 16, 32, "Intermediate"},
    {30, 16, 54, "Expert"},
}};
constexpr const std::array<Preset, 3>& presets(Mode mode) {
    return mode == Mode::hyper ? PRESETS_HYPER : PRESETS_COMPLEX;
}

// Feedback about the last action (the wording belongs to the UI layer; this is just a code).
enum class Msg : std::uint8_t {
    none = 0,
    started,
    judge_fail,
    expand_ok,  // msg_arg = number of neighbors involved in the expansion
    win,
    lose,
};

// PCG32 (XSH RR variant, by Melissa O'Neill): a small, fast generator with good statistical
// quality. The same seed always gives the same sequence, hence the same board for the same first
// click.
class Rng {
public:
    // A game seed selects the state; the stream is fixed. Seed 0 is treated as 1.
    explicit Rng(std::uint32_t seed = 1) { reseed(seed == 0 ? 1 : seed, kStream); }
    // The generator's own seeding routine: `initstate` picks the starting point and `initseq` the
    // stream (sequence).
    Rng(std::uint64_t initstate, std::uint64_t initseq) { reseed(initstate, initseq); }

    // 32 uniformly distributed bits.
    [[nodiscard]] std::uint32_t nextU32();
    // Returns a value in [0, 1).
    [[nodiscard]] double next();
    // Returns a uniformly distributed value in [0, n) without modulo bias, or 0 when n == 0.
    [[nodiscard]] std::size_t below(std::size_t n);

private:
    static constexpr std::uint64_t kStream = 0xda3e39cb94b95bdbULL;
    void reseed(std::uint64_t initstate, std::uint64_t initseq);

    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 1;
};

// Splits a total mine count as evenly as possible over the four types (indices 1..4; 0 unused).
[[nodiscard]] std::array<std::uint16_t, 5> splitEvenly(std::uint16_t total);

// The 8-neighborhood of a cell (cells outside the board are left out). Usable in a range-for.
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
    // Total number of cells. Only newGame() recomputes it from w and h, so call newGame() after
    // changing w or h.
    std::size_t n = 81;
    // Current mode (newGame leaves it alone: the mode is a setting, not part of the position).
    Mode mode = Mode::complex;
    // In Minkowski mode, use the looser expansion criterion: only a^2 - b^2 of the flags has to
    // match the truth.
    bool judge_loose = false;

    std::array<std::uint8_t, MAX_CELLS> mine{};  // 0 = no mine, 1..4 = mine type
    // Displayed value: a^2 + b^2 in complex mode (0..64), a^2 - b^2 in Minkowski mode (-64..64).
    // Mine cells and cells not computed yet hold -1.
    std::array<std::int16_t, MAX_CELLS> clue{};
    std::array<std::uint8_t, MAX_CELLS> open{};
    std::array<std::uint8_t, MAX_CELLS> flag{};  // 0 = no flag, 1..4 = flag type

    std::uint32_t seed = 1;
    Rng rng{1};
    std::uint16_t mines = 10;
    // Number of mines of each type (indices 1..4), public from the start of the game.
    std::array<std::uint16_t, 5> type_total{};
    std::array<std::uint16_t, 5> flags_of{};
    // Custom per-type mine counts (indices 1..4); all zero = random types.
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
    // Number of flags in the neighborhood.
    [[nodiscard]] std::size_t nbrFlagCount(std::size_t cell) const;
    // A blank cell has no mine itself and none around it; only blank cells cascade.
    [[nodiscard]] bool isBlank(std::size_t cell) const;
    [[nodiscard]] std::size_t flagsTotal() const;
    // How many mines of type t are still unflagged (may be negative).
    [[nodiscard]] std::int32_t unmarked(std::size_t t) const;

    void setMsg(Msg m, std::uint16_t arg = 0);
    void setSeed(std::uint32_t s);

    // Starts a new game (the board is generated on the first click). w and h are clamped to
    // [1, MAX] and mines to [0, MAX_MINES].
    void newGame(std::uint32_t new_seed);
    // The first click: reseeds the generator, lays the mines, computes the clues and cascades
    // from the clicked cell.
    void startAt(std::size_t cell, std::uint32_t now_ms);
    // Lays mines and computes clues. The start cell and its neighbors are guaranteed mine-free.
    void genBoard(std::size_t start);
    void computeClues();
    void countTypes();

    // Neighborhood sums (a, b): a = (+1 mines) - (-1 mines), b = (+unit mines) - (-unit mines).
    // Counts flags when use_flag is true, real mines otherwise.
    [[nodiscard]] std::array<int, 2> sumsOf(std::size_t cell, bool use_flag) const;

    // Opens cells in a cascade: it only spreads through blank cells and goes around flagged
    // cells. Returns the number of newly opened cells.
    std::size_t cascadeOpen(std::span<const std::uint16_t> seeds);

    // Places / changes / clears a flag (unlimited). t must be in 0..4, otherwise returns false.
    bool setFlag(std::size_t cell, std::uint8_t t);
    // Right click cycle: none -> +1 -> -1 -> +i -> -i -> none. Flagging can win the game.
    bool cycleFlag(std::size_t cell);
    // Opens a cell; a flagged cell cannot be opened.
    void reveal(std::size_t cell);

    // Expansion criterion: the number of flags equals the number of real mines around the cell
    // and any difference the displayed value cannot reveal is tolerated.
    //   Complex mode: the real/imaginary split of the flags may equal the true split or its swap.
    //   Minkowski mode: |a| and |b| must each match; with judge_loose only a^2 - b^2 must match.
    [[nodiscard]] bool matchComboTruth(std::size_t cell) const;
    // Chord: if the criterion holds, open every unflagged neighbor; hitting a mine loses.
    void tryExpand(std::size_t cell);
    // The game is won when the flags are exactly on the mines and the vectors of the flags around
    // every open cell add up to the number shown on it. Flag types need not match how the board
    // was generated: any labeling that reproduces all the numbers counts. Opening every safe cell
    // does not win by itself; the mines must be flagged too.
    void checkWin();
    [[nodiscard]] bool flagsSolve() const;
    // Displayed value of a neighborhood whose mine vectors sum to (a, b).
    [[nodiscard]] int valueOf(int a, int b) const;
    void lose(std::size_t cell);

    [[nodiscard]] std::size_t openedCount() const;
    [[nodiscard]] std::size_t safeCount() const;
    [[nodiscard]] std::size_t correctFlags() const;
    [[nodiscard]] std::size_t typeSum() const;

private:
    [[nodiscard]] bool validCell(std::size_t cell) const { return cell < n; }
};

}  // namespace cw
