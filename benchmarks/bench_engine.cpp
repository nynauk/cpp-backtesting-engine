// Runtime benchmark. Times CSV loading and the backtest loop separately, repeats the loop and reports the best
// and median times, so a speed claim always comes with its measurement procedure.
//
//   bt_bench --data eurusd_1m.csv --repeat 5 --short 20 --long 50 [--limit N]
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <iostream>
#include <limits>
#include <map>
#include <string>
#include <vector>

#include "bt/csv.hpp"
#include "bt/engine.hpp"
#include "bt/metrics.hpp"
#include "bt/strategy.hpp"

int main(int argc, char** argv) {
    std::map<std::string, std::string> a = {{"--repeat", "5"}, {"--short", "20"}, {"--long", "50"}, {"--limit", "0"}};
    for (int i = 1; i + 1 < argc; i += 2) a[argv[i]] = argv[i + 1];
    if (!a.count("--data")) {
        std::cerr << "usage: bt_bench --data FILE [--repeat N] [--short N] [--long N] [--limit BARS]\n";
        return 2;
    }
    using clock = std::chrono::steady_clock;
    try {
        const auto t0 = clock::now();
        auto bars = bt::load_bars_csv(a["--data"]);
        const double load_s = std::chrono::duration<double>(clock::now() - t0).count();
        const std::size_t limit = std::stoul(a["--limit"]);
        if (limit > 0 && limit < bars.size()) bars.resize(limit);

        bt::EngineConfig cfg;
        cfg.costs.basis = bt::PriceBasis::Bid;
        cfg.costs.spread = 0.0001;
        cfg.costs.commission_per_unit = 0.000035;
        // A stop-out would end trading early and make later bars cheaper; switch it off so every bar does the
        // full fill / mark / signal cycle.
        cfg.stop_out_equity = -std::numeric_limits<double>::infinity();
        bt::MovingAverageCrossover strategy(std::stoul(a["--short"]), std::stoul(a["--long"]), 100000.0);

        const int repeat = std::max(1, std::stoi(a["--repeat"]));
        std::vector<double> times;
        double checksum = 0.0;
        std::size_t fills = 0;
        for (int r = 0; r < repeat; ++r) {
            const auto s = clock::now();
            const auto result = bt::run_backtest(bars, strategy, cfg);
            times.push_back(std::chrono::duration<double>(clock::now() - s).count());
            checksum = result.equity.back().equity;  // keeps the work observable
            fills = result.fills.size();
        }
        std::sort(times.begin(), times.end());
        const double best = times.front(), median = times[times.size() / 2];
#if defined(_MSC_VER)
        const std::string compiler = "MSVC " + std::to_string(_MSC_VER);
#else
        const std::string compiler = __VERSION__;
#endif
#ifdef NDEBUG
        const char* build = "release (NDEBUG)";
#else
        const char* build = "debug";
#endif
        std::printf(
            "{\n  \"bars\": %zu,\n  \"load_seconds\": %.3f,\n  \"run_seconds_best\": %.4f,\n  \"run_seconds_median\": %.4f,\n"
            "  \"bars_per_second_best\": %.0f,\n  \"repeats\": %d,\n  \"fills\": %zu,\n  \"final_equity\": %.2f,\n"
            "  \"compiler\": \"%s\",\n  \"build\": \"%s\"\n}\n",
            bars.size(), load_s, best, median, static_cast<double>(bars.size()) / best, repeat, fills, checksum,
            compiler.c_str(), build);
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
