#include "bt/metrics.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace bt {
namespace {

// Welford's running mean and variance.
struct Running {
    std::size_t n = 0;
    double mean = 0.0, m2 = 0.0;
    void add(double x) {
        ++n;
        const double d = x - mean;
        mean += d / static_cast<double>(n);
        m2 += d * (x - mean);
    }
    double sd() const { return n > 1 ? std::sqrt(m2 / static_cast<double>(n - 1)) : 0.0; }
};

}  // namespace

Metrics compute_metrics(const BacktestResult& r) {
    Metrics m;
    m.fills = r.fills.size();
    m.fees_paid = r.fees_paid;
    m.spread_slippage_cost = r.spread_slippage_cost;
    m.stopped_out = r.stopped_out;
    m.stop_out_timestamp = r.stop_out_timestamp;
    const auto nan = std::numeric_limits<double>::quiet_NaN();
    if (r.equity.empty()) {
        m.final_equity = r.initial_cash;
        m.cagr = m.annual_volatility = m.sharpe = nan;
        m.annual_return_on_capital = m.annual_volatility_on_capital = m.sharpe_pnl = nan;
        return m;
    }

    const std::size_t n = r.equity.size();
    Running returns, pnl;
    double prev = r.initial_cash, peak = r.initial_cash;
    std::size_t in_market = 0;
    for (std::size_t i = 0; i < n; ++i) {
        const double e = r.equity[i].equity;
        if (e <= 0.0) m.ruined = true;
        returns.add(prev > 0.0 ? e / prev - 1.0 : 0.0);  // discarded below if the account was ruined
        pnl.add(e - prev);
        peak = std::max(peak, e);
        m.max_drawdown = std::min(m.max_drawdown, e / peak - 1.0);
        m.max_drawdown_on_capital = std::min(m.max_drawdown_on_capital, (e - peak) / r.initial_cash);
        if (r.equity[i].position != 0.0) ++in_market;
        prev = e;
    }
    m.observations = n;
    m.final_equity = r.equity.back().equity;
    m.total_return = m.final_equity / r.initial_cash - 1.0;
    m.net_pnl = m.final_equity - r.initial_cash;
    m.max_drawdown = std::max(m.max_drawdown, -1.0);
    m.exposure = static_cast<double>(in_market) / static_cast<double>(n);

    const double seconds = static_cast<double>(r.equity.back().timestamp - r.equity.front().timestamp);
    m.years = seconds / (365.25 * 86400.0);
    if (m.years <= 0.0 || n < 2) {
        m.periods_per_year = m.cagr = m.annual_volatility = m.sharpe = nan;
        m.annual_return_on_capital = m.annual_volatility_on_capital = m.sharpe_pnl = m.turnover = nan;
        return m;
    }
    m.periods_per_year = static_cast<double>(n) / m.years;
    const double root_ppy = std::sqrt(m.periods_per_year);

    m.annual_return_on_capital = pnl.mean * m.periods_per_year / r.initial_cash;
    m.annual_volatility_on_capital = pnl.sd() * root_ppy / r.initial_cash;
    m.sharpe_pnl = pnl.sd() > 0.0 ? pnl.mean / pnl.sd() * root_ppy : nan;
    m.turnover = r.traded_notional / r.initial_cash / m.years;

    if (m.ruined) {
        m.cagr = m.annual_volatility = m.sharpe = nan;
    } else {
        m.annual_volatility = returns.sd() * root_ppy;
        m.sharpe = returns.sd() > 0.0 ? returns.mean / returns.sd() * root_ppy : nan;
        m.cagr = std::pow(m.final_equity / r.initial_cash, 1.0 / m.years) - 1.0;
    }
    return m;
}

}  // namespace bt
