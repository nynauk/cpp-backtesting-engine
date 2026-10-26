#pragma once
// The event loop. Execution convention, applied to every strategy:
//   1. At the open of bar t, fill the target the strategy set after bar t-1 closed (market order at the
//      open, adjusted for spread and slippage, plus commission).
//   2. Mark the position to bar t's close and record equity.
//   3. Let the strategy see bars[0..t] and set the target for bar t+1's open.
// A signal can therefore never trade on the price that produced it. A target set after the final bar is
// never filled; the last equity point is marked to market, not liquidated.
//
// Evaluation window: equity is recorded only for bars in [start, end). Bars before `start` are still shown to
// the strategy, so indicators are warmed up on earlier history instead of eating into the test period.
//
// Stop-out: a fixed-size strategy can lose more than the account holds. When equity at a close is at or below
// `stop_out_equity`, the engine closes the position at the next open (with normal costs) and stops trading for
// the rest of the run, as a broker would. The loss can overshoot the level, because the check happens at
// closes and the exit at the next open.

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

#include "bt/costs.hpp"
#include "bt/strategy.hpp"
#include "bt/types.hpp"

namespace bt {

struct EngineConfig {
    double initial_cash = 100000.0;
    CostModel costs;
    std::int64_t start = std::numeric_limits<std::int64_t>::min();  // inclusive
    std::int64_t end = std::numeric_limits<std::int64_t>::max();    // exclusive
    double stop_out_equity = 0.0;  // -infinity disables the stop-out
};

struct EquityPoint {
    std::int64_t timestamp = 0;
    double close = 0.0;
    double position = 0.0;
    double cash = 0.0;
    double equity = 0.0;
};

struct BacktestResult {
    std::vector<EquityPoint> equity;
    std::vector<Fill> fills;
    double initial_cash = 0.0;
    double realised_pnl = 0.0;
    double unrealised_pnl = 0.0;
    double fees_paid = 0.0;
    double traded_notional = 0.0;   // sum of quantity * price over fills
    double spread_slippage_cost = 0.0;  // sum of quantity * |fill price - quote|
    bool stopped_out = false;
    std::int64_t stop_out_timestamp = 0;  // close at which equity breached the stop-out level
};

BacktestResult run_backtest(const std::vector<Bar>& bars, Strategy& strategy, const EngineConfig& config);

}  // namespace bt
