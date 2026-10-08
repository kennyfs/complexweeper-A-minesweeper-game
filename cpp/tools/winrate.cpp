// Measures how often the solver wins a given board setup.
//
//   winrate <mode 0|1> <width> <height> <mines> <games> <threads> [first_seed] [orientation|random]
//
// Prints one line: wins games stuck games_with_reorientation guesses steps
// Every game is independent (seed = first_seed + game index), so the games are spread over the
// threads. A game the solver gives up on before the game is over counts as a loss ("stuck").
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

#include "session.hpp"

int main(int argc, char** argv) {
    if (argc < 7) {
        std::fprintf(stderr, "usage: %s <mode 0|1> <w> <h> <mines> <games> <threads> [first_seed] [orientation|random]\n", argv[0]);
        return 2;
    }
    const cw::Mode mode = std::atoi(argv[1]) == 1 ? cw::Mode::hyper : cw::Mode::complex;
    const int w = std::atoi(argv[2]), h = std::atoi(argv[3]), mines = std::atoi(argv[4]);
    const int games = std::atoi(argv[5]);
    const int threads = std::max(1, std::atoi(argv[6]));
    const std::uint32_t seed0 = argc > 7 ? static_cast<std::uint32_t>(std::strtoul(argv[7], nullptr, 10)) : 1;
    const bool random_orientation = argc > 8 && std::strcmp(argv[8], "random") == 0;
    const int fixed_orientation = argc > 8 && !random_orientation ? std::atoi(argv[8]) : 0;

    std::atomic<int> next{0}, wins{0}, stuck{0}, reoriented{0};
    std::atomic<long> guesses{0}, steps{0};
    const auto worker = [&] {
        for (int k = next++; k < games; k = next++) {
            cw::Session s;
            const std::uint32_t seed = seed0 + static_cast<std::uint32_t>(k);
            s.setSolverOrientation(random_orientation ? static_cast<int>(seed % 8) : fixed_orientation);
            s.newGame(static_cast<std::uint16_t>(w), static_cast<std::uint16_t>(h), static_cast<std::uint16_t>(mines),
                      mode, seed);
            bool retyped = false;
            long my_guesses = 0, my_steps = 0;
            while (!s.game.over) {
                const cw::Move m = s.solverStep(0);
                if (m.kind == cw::MoveKind::none) break;
                ++my_steps;
                my_guesses += m.reason == cw::Reason::guess;
                retyped = retyped || m.retyped != 0;
            }
            if (!s.game.over) ++stuck;
            wins += s.game.win;
            reoriented += retyped;
            guesses += my_guesses;
            steps += my_steps;
        }
    };
    // The calling thread works too, so the process runs exactly `threads` threads (never more).
    std::vector<std::thread> pool;
    for (int t = 1; t < threads; ++t) pool.emplace_back(worker);
    worker();
    for (auto& t : pool) t.join();
    std::printf("%d %d %d %d %ld %ld\n", wins.load(), games, stuck.load(), reoriented.load(), guesses.load(),
                steps.load());
    return 0;
}
