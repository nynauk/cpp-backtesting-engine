#pragma once
// Performance metrics from the equity curve (never from isolated winning trades).
//
// Conventions, stated next to every result: the annualisation factor is estimated from the data
// (observations per calendar year over the evaluation window), so it suits FX hours or minutes without a
// hard-coded constant; Sharpe ratios use a zero risk-free rate; the first bar's change is measured from the
// initial cash, so a fill on the first bar is counted.
//
// Two views of the same equity curve:
//   * Compounding: bar returns are equity_t / equity_{t-1} - 1. Undefined once equity reaches zero, so for a
//     ruined account these are reported as NaN (null in JSON) and max_drawdown as -100%.
//   * Fixed capital: bar P&L in currency, scaled by the initial cash, no compounding. This is the natural
//     scale for strategies that trade a fixed number of units, and it stays defined after a ruinous loss.

#include <cstddef>
#include <cstdint>

#include "bt/engine.hpp"

namespace bt {

struct Metrics {
    std::size_t observations = 0;
    double years = 0.0;
    double periods_per_year = 0.0;
    double final_equity = 0.0;
    bool ruined = false;  // equity at or below zero at some close
    bool stopped_out = false;
    std::int64_t stop_out_timestamp = 0;

    // Compounding view.
    double total_return = 0.0;
    double cagr = 0.0;
    double annual_volatility = 0.0;
    double sharpe = 0.0;
    double max_drawdown = 0.0;  // negative fraction of the running equity peak, e.g. -0.25

    // Fixed-capital view.
    double net_pnl = 0.0;
    double annual_return_on_capital = 0.0;      // mean bar P&L * periods per year / initial cash
    double annual_volatility_on_capital = 0.0;  // sd of bar P&L * sqrt(periods per year) / initial cash
    double sharpe_pnl = 0.0;                    // mean / sd of bar P&L * sqrt(periods per year)
    double max_drawdown_on_capital = 0.0;       // worst fall from an equity peak, as a fraction of initial cash

    std::size_t fills = 0;
    double exposure = 0.0;  // share of bars with an open position
    double turnover = 0.0;  // traded notional per year, in multiples of the initial cash
    double fees_paid = 0.0;
    double spread_slippage_cost = 0.0;
};

Metrics compute_metrics(const BacktestResult& result);

}  // namespace bt
