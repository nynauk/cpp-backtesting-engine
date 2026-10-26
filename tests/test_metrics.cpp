#include <gtest/gtest.h>

#include <cmath>
#include <vector>

#include "bt/csv.hpp"
#include "bt/metrics.hpp"

namespace {

bt::BacktestResult result_from(const std::vector<double>& equity, std::int64_t step_seconds, double cash = 100.0) {
    bt::BacktestResult r;
    r.initial_cash = cash;
    std::int64_t t = bt::parse_timestamp("2020-01-01");
    for (double e : equity) {
        r.equity.push_back({t, 1.0, 0.0, e, e});
        t += step_seconds;
    }
    return r;
}

}  // namespace

TEST(Metrics, MaxDrawdownIsTheWorstPeakToTroughFall) {
    // Peak 120 -> 90 is -25%; the later 130 -> 117 fall is only -10%.
    const auto m = bt::compute_metrics(result_from({120, 90, 130, 117}, 86400));
    EXPECT_NEAR(m.max_drawdown, -0.25, 1e-12);
    EXPECT_NEAR(m.total_return, 0.17, 1e-12);
}

TEST(Metrics, CagrAnnualisesOverTheCalendarSpan) {
    // 100 -> 121 between two timestamps exactly two (365.25-day) years apart is 10% a year.
    const std::int64_t two_years = static_cast<std::int64_t>(2 * 365.25 * 86400);
    const auto m = bt::compute_metrics(result_from({100, 121}, two_years));
    EXPECT_NEAR(m.years, 2.0, 1e-12);
    EXPECT_NEAR(m.cagr, 0.10, 1e-12);
}

TEST(Metrics, SharpeMatchesTheTextbookFormula) {
    const std::vector<double> equity = {101, 100, 102, 103, 101, 104};
    const auto m = bt::compute_metrics(result_from(equity, 86400));
    std::vector<double> rets;
    double prev = 100.0;
    for (double e : equity) {
        rets.push_back(e / prev - 1.0);
        prev = e;
    }
    double mean = 0.0;
    for (double x : rets) mean += x;
    mean /= static_cast<double>(rets.size());
    double ss = 0.0;
    for (double x : rets) ss += (x - mean) * (x - mean);
    const double sd = std::sqrt(ss / static_cast<double>(rets.size() - 1));
    const double ppy = static_cast<double>(rets.size()) / m.years;
    EXPECT_NEAR(m.sharpe, mean / sd * std::sqrt(ppy), 1e-9);
    EXPECT_NEAR(m.annual_volatility, sd * std::sqrt(ppy), 1e-12);
}

TEST(Metrics, ExposureCountsBarsWithAPosition) {
    auto r = result_from({100, 100, 100, 100}, 3600);
    r.equity[1].position = 1000;
    r.equity[2].position = -1000;
    EXPECT_NEAR(bt::compute_metrics(r).exposure, 0.5, 1e-12);
}

TEST(Metrics, FixedCapitalViewUsesCurrencyPnl) {
    // P&L per bar: +10, -5, +15 on 100 of capital. Peak 110 -> 105 is a 5% fall measured on capital.
    const auto m = bt::compute_metrics(result_from({110, 105, 120}, 86400));
    const double mean = 20.0 / 3.0;
    const double sd = std::sqrt(((10 - mean) * (10 - mean) + (-5 - mean) * (-5 - mean) + (15 - mean) * (15 - mean)) / 2.0);
    EXPECT_NEAR(m.net_pnl, 20.0, 1e-12);
    EXPECT_NEAR(m.sharpe_pnl, mean / sd * std::sqrt(m.periods_per_year), 1e-9);
    EXPECT_NEAR(m.annual_return_on_capital, mean * m.periods_per_year / 100.0, 1e-9);
    EXPECT_NEAR(m.annual_volatility_on_capital, sd * std::sqrt(m.periods_per_year) / 100.0, 1e-9);
    EXPECT_NEAR(m.max_drawdown_on_capital, -0.05, 1e-12);
    EXPECT_NEAR(m.max_drawdown, 105.0 / 110.0 - 1.0, 1e-12);
}

TEST(Metrics, RuinLeavesCompoundingMetricsUndefined) {
    const auto m = bt::compute_metrics(result_from({50, -10, 5}, 86400));
    EXPECT_TRUE(m.ruined);
    EXPECT_TRUE(std::isnan(m.sharpe));
    EXPECT_TRUE(std::isnan(m.cagr));
    EXPECT_TRUE(std::isnan(m.annual_volatility));
    EXPECT_DOUBLE_EQ(m.max_drawdown, -1.0);
    // The fixed-capital view is still meaningful: lost 110% of capital at the worst point.
    EXPECT_NEAR(m.max_drawdown_on_capital, -1.10, 1e-12);
    EXPECT_NEAR(m.net_pnl, -95.0, 1e-12);
    EXPECT_FALSE(std::isnan(m.sharpe_pnl));
}

TEST(Metrics, EmptyResultIsNotAnError) {
    bt::BacktestResult r;
    r.initial_cash = 100.0;
    const auto m = bt::compute_metrics(r);
    EXPECT_EQ(m.observations, 0u);
    EXPECT_DOUBLE_EQ(m.final_equity, 100.0);
    EXPECT_TRUE(std::isnan(m.sharpe));
}
