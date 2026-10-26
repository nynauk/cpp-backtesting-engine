#include "bt/engine.hpp"

#include <cmath>
#include <stdexcept>

#include "bt/portfolio.hpp"

namespace bt {

BacktestResult run_backtest(const std::vector<Bar>& bars, Strategy& strategy, const EngineConfig& cfg) {
    if (cfg.start >= cfg.end) throw std::invalid_argument("evaluation start must be before end");
    if (!(cfg.stop_out_equity < cfg.initial_cash))
        throw std::invalid_argument("stop-out level must be below the initial cash");
    strategy.reset();
    Portfolio portfolio(cfg.initial_cash);
    BacktestResult result;
    result.initial_cash = cfg.initial_cash;
    result.equity.reserve(bars.size());

    double pending_target = 0.0;
    bool have_pending = false;
    bool liquidating = false;  // stop-out hit at the last close: flatten at this open
    bool halted = false;       // stopped out: no more orders for the rest of the run
    for (std::size_t t = 0; t < bars.size(); ++t) {
        const Bar& bar = bars[t];
        if (bar.timestamp >= cfg.end) break;
        const bool in_window = bar.timestamp >= cfg.start;

        // 1. Fill the previous close's target (or the forced exit) at this bar's open.
        if (in_window && (have_pending || liquidating)) {
            const double target = liquidating ? 0.0 : pending_target;
            const double delta = target - portfolio.position();
            if (std::abs(delta) > 1e-9) {
                Fill f;
                f.timestamp = bar.timestamp;
                f.side = delta > 0.0 ? Side::Buy : Side::Sell;
                f.quantity = std::abs(delta);
                f.price = execution_price(cfg.costs, f.side, bar.open);
                f.fee = commission(cfg.costs, f.quantity, f.price);
                portfolio.apply_fill(f);
                result.fills.push_back(f);
                result.traded_notional += f.quantity * f.price;
                result.spread_slippage_cost += f.quantity * std::abs(f.price - bar.open);
            }
            if (liquidating) {
                liquidating = false;
                halted = true;
                have_pending = false;
            }
        }

        // 2. Mark to the close, record equity, check the stop-out.
        if (in_window) {
            portfolio.mark_to_market(bar.close);
            result.equity.push_back({bar.timestamp, bar.close, portfolio.position(), portfolio.cash(), portfolio.equity()});
            if (!halted && !liquidating && portfolio.equity() <= cfg.stop_out_equity) {
                result.stopped_out = true;
                result.stop_out_timestamp = bar.timestamp;
                if (portfolio.position() != 0.0) liquidating = true;
                else halted = true;
                have_pending = false;
            }
        }

        // 3. The strategy sees bars[0..t] only and sets the next target.
        if (!halted && !liquidating) {
            pending_target = strategy.target_position(History(bars.data(), t + 1));
            have_pending = true;
        }
    }

    result.realised_pnl = portfolio.realised_pnl();
    result.unrealised_pnl = portfolio.unrealised_pnl();
    result.fees_paid = portfolio.fees_paid();
    return result;
}

}  // namespace bt
