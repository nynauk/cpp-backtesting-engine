#include <cmath>
#include <stdexcept>

#include "bt/csv.hpp"
#include "bt/strategy.hpp"

namespace bt {

MovingAverageCrossover::MovingAverageCrossover(std::size_t short_window, std::size_t long_window, double units)
    : short_window_(short_window), long_window_(long_window), units_(units) {
    if (short_window == 0 || long_window == 0) throw std::invalid_argument("moving-average windows must be positive");
    if (short_window >= long_window) throw std::invalid_argument("short window must be shorter than long window");
    if (!(units > 0.0)) throw std::invalid_argument("units must be positive");
}

void MovingAverageCrossover::reset() {
    short_sum_ = long_sum_ = 0.0;
    next_t_ = 0;
    last_target_ = 0.0;
}

double MovingAverageCrossover::target_position(const History& bars) {
    const std::size_t t = bars.last_index();
    if (t != next_t_) throw std::logic_error("MovingAverageCrossover must see every bar in order from t = 0");
    ++next_t_;

    // Running window sums, recomputed from scratch every 4096 bars so rounding drift cannot build up.
    if (t % 4096 == 0) {
        short_sum_ = long_sum_ = 0.0;
        for (std::size_t i = (t + 1 > short_window_ ? t + 1 - short_window_ : 0); i < t; ++i) short_sum_ += bars[i].close;
        for (std::size_t i = (t + 1 > long_window_ ? t + 1 - long_window_ : 0); i < t; ++i) long_sum_ += bars[i].close;
        short_sum_ += bars[t].close;
        long_sum_ += bars[t].close;
    } else {
        short_sum_ += bars[t].close;
        long_sum_ += bars[t].close;
        if (t >= short_window_) short_sum_ -= bars[t - short_window_].close;
        if (t >= long_window_) long_sum_ -= bars[t - long_window_].close;
    }

    if (t + 1 < long_window_) return 0.0;  // warm-up: no signal until the long window is full

    const double short_ma = short_sum_ / static_cast<double>(short_window_);
    const double long_ma = long_sum_ / static_cast<double>(long_window_);
    const double tolerance = 1e-12 * std::abs(long_ma);
    if (short_ma - long_ma > tolerance) last_target_ = units_;
    else if (long_ma - short_ma > tolerance) last_target_ = -units_;
    return last_target_;
}

TargetSeries::TargetSeries(std::vector<TimedTarget> targets) : targets_(std::move(targets)) {
    for (std::size_t i = 0; i < targets_.size(); ++i) {
        if (!std::isfinite(targets_[i].target)) throw std::invalid_argument("target positions must be finite");
        if (i > 0 && targets_[i].timestamp <= targets_[i - 1].timestamp)
            throw std::invalid_argument("target timestamps must be strictly increasing");
    }
}

double TargetSeries::target_position(const History& history) {
    // Bars arrive in time order, so one forward-moving cursor finds each bar's entry.
    const std::int64_t now = history.back().timestamp;
    while (next_ < targets_.size() && targets_[next_].timestamp < now) ++next_;
    if (next_ < targets_.size() && targets_[next_].timestamp == now) return targets_[next_++].target;
    return 0.0;
}

void check_targets_match_bars(const std::vector<Bar>& bars, const std::vector<TimedTarget>& targets) {
    std::size_t b = 0;
    for (const auto& t : targets) {
        while (b < bars.size() && bars[b].timestamp < t.timestamp) ++b;
        if (b == bars.size() || bars[b].timestamp != t.timestamp)
            throw std::runtime_error("target at " + format_timestamp(t.timestamp) + " has no matching bar");
    }
}

}  // namespace bt
