#include <gtest/gtest.h>

#include <cmath>
#include <limits>
#include <random>
#include <vector>

#include "bt/csv.hpp"
#include "bt/engine.hpp"
#include "bt/metrics.hpp"
#include "bt/strategy.hpp"

namespace {

// Returns a pre-set target after each bar: lets a test spell out exactly what should be traded.
class Scripted : public bt::Strategy {
public:
    explicit Scripted(std::vector<double> targets) : targets_(std::move(targets)) {}
    double target_position(const bt::History& h) override {
        const std::size_t t = h.last_index();
        return t < targets_.size() ? targets_[t] : targets_.back();
    }

private:
    std::vector<double> targets_;
};

// Records what it was shown, then tries to read the next bar: the engine must not make that possible.
class Peeking : public bt::Strategy {
public:
    double target_position(const bt::History& h) override {
        sizes.push_back(h.size());
        last_seen.push_back(h.back().timestamp);
        if (peek) return h[h.size()].close > h.back().close ? 1000.0 : -1000.0;
        return 0.0;
    }
    bool peek = false;
    std::vector<std::size_t> sizes;
    std::vector<std::int64_t> last_seen;
};

bt::Bar bar(std::int64_t hour, double open, double close) {
    bt::Bar b;
    b.timestamp = bt::parse_timestamp("2024-01-01") + hour * 3600;
    b.open = open;
    b.close = close;
    b.high = std::max(open, close);
    b.low = std::min(open, close);
    return b;
}

std::vector<bt::Bar> flat_bars(std::size_t n, double price) {
    std::vector<bt::Bar> v;
    for (std::size_t i = 0; i < n; ++i) v.push_back(bar(static_cast<std::int64_t>(i), price, price));
    return v;
}

std::vector<bt::Bar> random_walk(std::size_t n, unsigned seed) {
    std::mt19937 rng(seed);
    std::normal_distribution<double> step(0.0, 0.0008);
    std::vector<bt::Bar> v;
    double p = 1.10;
    for (std::size_t i = 0; i < n; ++i) {
        const double open = p;
        p *= 1.0 + step(rng);
        v.push_back(bar(static_cast<std::int64_t>(i), open, p));
    }
    return v;
}

}  // namespace

TEST(Engine, FlatPricesGiveZeroGrossReturnForBuyAndHold) {
    const auto bars = flat_bars(50, 1.2345);
    bt::BuyAndHold hold(100000);
    bt::EngineConfig cfg;  // no costs
    const auto r = bt::run_backtest(bars, hold, cfg);
    ASSERT_EQ(r.fills.size(), 1u);
    EXPECT_DOUBLE_EQ(r.equity.back().equity, cfg.initial_cash);

    cfg.costs.commission_per_unit = 0.00002;  // $2 per 100k: the only change in equity should be that fee
    const auto with_fee = bt::run_backtest(bars, hold, cfg);
    EXPECT_NEAR(with_fee.equity.back().equity, cfg.initial_cash - 2.0, 1e-9);
}

TEST(Engine, NoTradeStrategyKeepsEquityExactlyAtStartingCash) {
    const auto bars = random_walk(500, 1);
    bt::NoTrade none;
    const auto r = bt::run_backtest(bars, none, bt::EngineConfig{});
    EXPECT_TRUE(r.fills.empty());
    for (const auto& p : r.equity) ASSERT_DOUBLE_EQ(p.equity, 100000.0);
}

TEST(Engine, SignalsFillAtTheNextBarsOpenNeverTheSameClose) {
    const std::vector<bt::Bar> bars = {bar(0, 1.00, 1.10), bar(1, 1.20, 1.30), bar(2, 1.40, 1.50)};
    Scripted s({1000.0});
    const auto r = bt::run_backtest(bars, s, bt::EngineConfig{});
    ASSERT_EQ(r.fills.size(), 1u);
    EXPECT_EQ(r.fills[0].timestamp, bars[1].timestamp);  // decided after bar 0 closed
    EXPECT_DOUBLE_EQ(r.fills[0].price, 1.20);             // bar 1's open, not bar 0's close (1.10)
}

TEST(Engine, NoTradeBeforeTheFirstValidSignal) {
    // Rising closes: MA(2) > MA(4) as soon as both exist. The long window is full after bar 3 closes,
    // so the first fill must be at bar 4's open and every earlier equity point must be untouched.
    std::vector<bt::Bar> bars;
    for (int i = 0; i < 8; ++i) bars.push_back(bar(i, 1.0 + 0.01 * i, 1.005 + 0.01 * i));
    bt::MovingAverageCrossover ma(2, 4, 1000);
    const auto r = bt::run_backtest(bars, ma, bt::EngineConfig{});
    ASSERT_FALSE(r.fills.empty());
    EXPECT_EQ(r.fills.front().timestamp, bars[4].timestamp);
    EXPECT_EQ(r.fills.front().side, bt::Side::Buy);
    for (int i = 0; i < 4; ++i) EXPECT_DOUBLE_EQ(r.equity[static_cast<std::size_t>(i)].equity, 100000.0);
}

TEST(Engine, MatchesAHandCalculatedPortfolio) {
    // Bars (open, close): 1.1000/1.1010, 1.1010/1.1030, 1.1030/1.1020, 1.1020/1.1000, 1.1000/1.0980.
    // Targets after each close: +1000, +1000, 0, -1000, -1000. Mid prices, 2-pip spread, $0.01 per 1,000 units.
    //   bar 1 open: buy 1000 at 1.1010 + 0.0001 = 1.1011, fee 0.01 -> cash 100000 - 1101.10 - 0.01 = 98898.89
    //               close 1.1030 -> equity 98898.89 + 1103.00 = 100001.89
    //   bar 2: no trade; close 1.1020 -> equity 100000.89
    //   bar 3 open: sell 1000 at 1.1020 - 0.0001 = 1.1019, fee 0.01 -> cash 98898.89 + 1101.90 - 0.01 = 100000.78
    //   bar 4 open: sell 1000 at 1.1000 - 0.0001 = 1.0999, fee 0.01 -> cash 101100.67, position -1000
    //               close 1.0980 -> equity 101100.67 - 1098.00 = 100002.67
    //   realised +0.80 (1.1019 - 1.1011), unrealised +1.90 (1.0999 - 1.0980), fees 0.03.
    const std::vector<bt::Bar> bars = {bar(0, 1.1000, 1.1010), bar(1, 1.1010, 1.1030), bar(2, 1.1030, 1.1020),
                                       bar(3, 1.1020, 1.1000), bar(4, 1.1000, 1.0980)};
    Scripted s({1000, 1000, 0, -1000, -1000});
    bt::EngineConfig cfg;
    cfg.costs.spread = 0.0002;
    cfg.costs.commission_per_unit = 0.00001;
    const auto r = bt::run_backtest(bars, s, cfg);

    ASSERT_EQ(r.fills.size(), 3u);
    EXPECT_NEAR(r.fills[0].price, 1.1011, 1e-12);
    EXPECT_NEAR(r.fills[1].price, 1.1019, 1e-12);
    EXPECT_NEAR(r.fills[2].price, 1.0999, 1e-12);
    const double expected[5] = {100000.00, 100001.89, 100000.89, 100000.78, 100002.67};
    for (int i = 0; i < 5; ++i) EXPECT_NEAR(r.equity[static_cast<std::size_t>(i)].equity, expected[i], 1e-8);
    EXPECT_NEAR(r.realised_pnl, 0.80, 1e-9);
    EXPECT_NEAR(r.unrealised_pnl, 1.90, 1e-9);
    EXPECT_NEAR(r.fees_paid, 0.03, 1e-12);
    EXPECT_DOUBLE_EQ(r.equity.back().position, -1000.0);
}

TEST(Engine, EvaluationWindowWarmsUpOnEarlierBars) {
    const auto bars = random_walk(300, 3);
    bt::MovingAverageCrossover ma(5, 20, 1000);
    bt::EngineConfig cfg;
    cfg.start = bars[100].timestamp;
    const auto r = bt::run_backtest(bars, ma, cfg);
    ASSERT_EQ(r.equity.size(), 200u);
    EXPECT_EQ(r.equity.front().timestamp, bars[100].timestamp);
    // The indicator was ready long before the window opened, so the first in-window bar already trades.
    ASSERT_FALSE(r.fills.empty());
    EXPECT_EQ(r.fills.front().timestamp, bars[100].timestamp);
}

TEST(Engine, HigherCostsNeverImproveTheSameTrades) {
    const auto bars = random_walk(5000, 11);
    bt::MovingAverageCrossover ma(10, 40, 100000);
    double previous = 1e300;
    for (double spread : {0.0, 0.00005, 0.0001, 0.0002, 0.0005}) {
        bt::EngineConfig cfg;
        cfg.costs.spread = spread;
        const auto r = bt::run_backtest(bars, ma, cfg);
        EXPECT_LT(r.equity.back().equity, previous);
        previous = r.equity.back().equity;
    }
}

TEST(Engine, CrossoverRejectsBadWindowsAndOutOfOrderCalls) {
    EXPECT_THROW(bt::MovingAverageCrossover(50, 20, 1), std::invalid_argument);
    EXPECT_THROW(bt::MovingAverageCrossover(0, 20, 1), std::invalid_argument);
    const auto bars = random_walk(10, 5);
    bt::MovingAverageCrossover ma(2, 4, 1);
    EXPECT_THROW(ma.target_position(bt::History(bars.data(), 4)), std::logic_error);  // skipped bars 0..2
}

TEST(Engine, StrategiesOnlySeeBarsThatHaveClosed) {
    const auto bars = random_walk(20, 9);
    Peeking honest;
    bt::run_backtest(bars, honest, bt::EngineConfig{});
    ASSERT_EQ(honest.sizes.size(), bars.size());
    for (std::size_t t = 0; t < bars.size(); ++t) {
        EXPECT_EQ(honest.sizes[t], t + 1);
        EXPECT_EQ(honest.last_seen[t], bars[t].timestamp);
    }

    Peeking cheat;
    cheat.peek = true;
    EXPECT_THROW(bt::run_backtest(bars, cheat, bt::EngineConfig{}), std::out_of_range);
}

TEST(Engine, StopOutFlattensAtTheNextOpenAndStopsTrading) {
    // $1,000 account holding 100,000 units. Bar 1 closes 1.5 cents down: equity 1000 - 1500 = -500, so the
    // engine exits at bar 2's open (1.08): cash -109000 + 108000 = -1000. Buy-and-hold would buy straight back;
    // a stopped-out account must not.
    const std::vector<bt::Bar> bars = {bar(0, 1.10, 1.10), bar(1, 1.10, 1.085), bar(2, 1.08, 1.12),
                                       bar(3, 1.12, 1.15), bar(4, 1.15, 1.20)};
    bt::BuyAndHold hold(100000);
    bt::EngineConfig cfg;
    cfg.initial_cash = 1000.0;
    const auto r = bt::run_backtest(bars, hold, cfg);
    ASSERT_TRUE(r.stopped_out);
    EXPECT_EQ(r.stop_out_timestamp, bars[1].timestamp);
    ASSERT_EQ(r.fills.size(), 2u);
    EXPECT_EQ(r.fills[1].side, bt::Side::Sell);
    EXPECT_EQ(r.fills[1].timestamp, bars[2].timestamp);
    EXPECT_DOUBLE_EQ(r.fills[1].price, 1.08);
    ASSERT_EQ(r.equity.size(), 5u);
    for (std::size_t i = 2; i < 5; ++i) {
        EXPECT_NEAR(r.equity[i].equity, -1000.0, 1e-6);
        EXPECT_DOUBLE_EQ(r.equity[i].position, 0.0);
    }

    const auto m = bt::compute_metrics(r);
    EXPECT_TRUE(m.ruined);
    EXPECT_TRUE(std::isnan(m.sharpe));
    EXPECT_DOUBLE_EQ(m.max_drawdown, -1.0);
    EXPECT_NEAR(m.net_pnl, -2000.0, 1e-6);
}

TEST(Engine, StopOutCanBeSwitchedOff) {
    const std::vector<bt::Bar> bars = {bar(0, 1.10, 1.10), bar(1, 1.10, 1.085), bar(2, 1.08, 1.12),
                                       bar(3, 1.12, 1.15), bar(4, 1.15, 1.20)};
    bt::BuyAndHold hold(100000);
    bt::EngineConfig cfg;
    cfg.initial_cash = 1000.0;
    cfg.stop_out_equity = -std::numeric_limits<double>::infinity();
    const auto r = bt::run_backtest(bars, hold, cfg);
    EXPECT_FALSE(r.stopped_out);
    EXPECT_EQ(r.fills.size(), 1u);
    EXPECT_NEAR(r.equity.back().equity, 1000.0 + 100000 * (1.20 - 1.10), 1e-6);  // 11,000

    cfg.stop_out_equity = 1000.0;  // at or above the starting cash makes no sense
    EXPECT_THROW(bt::run_backtest(bars, hold, cfg), std::invalid_argument);
}

TEST(Engine, BarsAfterTheWindowEndAreNeverShownToTheStrategy) {
    const auto bars = random_walk(50, 13);
    Peeking s;
    bt::EngineConfig cfg;
    cfg.end = bars[30].timestamp;  // exclusive
    const auto r = bt::run_backtest(bars, s, cfg);
    EXPECT_EQ(r.equity.size(), 30u);
    EXPECT_EQ(s.sizes.size(), 30u);
    EXPECT_EQ(s.last_seen.back(), bars[29].timestamp);
}
