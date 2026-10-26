// Command-line runner: load bars, run one strategy, print metrics as JSON, optionally write equity/fills/metrics.
//
//   bt_run --data eurusd_1h.csv --strategy ma --short 20 --long 50 --units 100000 \
//          --basis bid --spread 0.0001 --commission-per-unit 0.000035 --start 2015-01-01 --end 2020-01-01 --out results/run
//   bt_run --data eurusd_1h.csv --strategy file --targets model_targets.csv ...   (positions from another program)
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>

#include "bt/csv.hpp"
#include "bt/engine.hpp"
#include "bt/metrics.hpp"
#include "bt/report.hpp"
#include "bt/strategy.hpp"

namespace {

void usage() {
    std::cerr << "usage: bt_run --data FILE [--strategy ma|buyhold|none|file] [--targets FILE] [--short N] [--long N] [--units U]\n"
                 "              [--cash C] [--basis mid|bid] [--spread S] [--slippage S]\n"
                 "              [--commission-per-unit X] [--commission-rate X] [--start DATE] [--end DATE]\n"
                 "              [--stop-out EQUITY|none] [--out DIR]\n";
}

double to_double(const std::string& name, const std::string& v) {
    std::size_t used = 0;
    const double x = std::stod(v, &used);
    if (used != v.size()) throw std::invalid_argument("bad number for " + name + ": " + v);
    return x;
}

}  // namespace

int main(int argc, char** argv) {
    try {
        std::map<std::string, std::string> a = {{"--strategy", "ma"}, {"--short", "20"},   {"--long", "50"},
                                                {"--units", "100000"}, {"--cash", "100000"}, {"--basis", "mid"},
                                                {"--spread", "0"},      {"--slippage", "0"},  {"--commission-per-unit", "0"},
                                                {"--commission-rate", "0"}, {"--stop-out", "0"}};
        for (int i = 1; i < argc; ++i) {
            const std::string key = argv[i];
            if (key == "--help" || key == "-h") {
                usage();
                return 0;
            }
            if (key.rfind("--", 0) != 0 || i + 1 >= argc) throw std::invalid_argument("bad argument: " + key);
            a[key] = argv[++i];
        }
        if (!a.count("--data")) {
            usage();
            return 2;
        }

        bt::EngineConfig cfg;
        cfg.initial_cash = to_double("--cash", a["--cash"]);
        cfg.costs.basis = a["--basis"] == "bid" ? bt::PriceBasis::Bid : bt::PriceBasis::Mid;
        if (a["--basis"] != "bid" && a["--basis"] != "mid") throw std::invalid_argument("--basis must be mid or bid");
        cfg.costs.spread = to_double("--spread", a["--spread"]);
        cfg.costs.slippage = to_double("--slippage", a["--slippage"]);
        cfg.costs.commission_per_unit = to_double("--commission-per-unit", a["--commission-per-unit"]);
        cfg.costs.commission_rate = to_double("--commission-rate", a["--commission-rate"]);
        if (a.count("--start")) cfg.start = bt::parse_timestamp(a["--start"]);
        if (a.count("--end")) cfg.end = bt::parse_timestamp(a["--end"]);
        cfg.stop_out_equity = a["--stop-out"] == "none" ? -std::numeric_limits<double>::infinity()
                                                       : to_double("--stop-out", a["--stop-out"]);

        const auto bars = bt::load_bars_csv(a["--data"]);
        const double units = to_double("--units", a["--units"]);
        std::unique_ptr<bt::Strategy> strategy;
        if (a["--strategy"] == "ma") {
            strategy = std::make_unique<bt::MovingAverageCrossover>(std::stoul(a["--short"]), std::stoul(a["--long"]), units);
        } else if (a["--strategy"] == "buyhold") {
            strategy = std::make_unique<bt::BuyAndHold>(units);
        } else if (a["--strategy"] == "none") {
            strategy = std::make_unique<bt::NoTrade>();
        } else if (a["--strategy"] == "file") {
            if (!a.count("--targets")) throw std::invalid_argument("--strategy file needs --targets FILE");
            auto targets = bt::load_targets_csv(a["--targets"]);
            bt::check_targets_match_bars(bars, targets);
            strategy = std::make_unique<bt::TargetSeries>(std::move(targets));
        } else {
            throw std::invalid_argument("unknown strategy: " + a["--strategy"]);
        }

        const auto result = bt::run_backtest(bars, *strategy, cfg);
        const auto metrics = bt::compute_metrics(result);

        std::map<std::string, std::string> extra = {
            {"data", a["--data"]},       {"strategy", a["--strategy"]},  {"short", a["--short"]},
            {"long", a["--long"]},       {"units", a["--units"]},        {"basis", a["--basis"]},
            {"spread", a["--spread"]},   {"slippage", a["--slippage"]},  {"commission_per_unit", a["--commission-per-unit"]},
            {"commission_rate", a["--commission-rate"]}, {"stop_out_level", a["--stop-out"]},
            {"targets", a.count("--targets") ? a["--targets"] : std::string()},
            {"start", result.equity.empty() ? "" : bt::format_timestamp(result.equity.front().timestamp)},
            {"end", result.equity.empty() ? "" : bt::format_timestamp(result.equity.back().timestamp)}};
        const std::string json = bt::metrics_json(metrics, extra);
        std::cout << json;

        if (a.count("--out")) {
            const std::filesystem::path out = a["--out"];
            std::filesystem::create_directories(out);
            bt::write_equity_csv((out / "equity.csv").string(), result);
            bt::write_fills_csv((out / "fills.csv").string(), result);
            bt::write_text((out / "metrics.json").string(), json);
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
