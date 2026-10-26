#include <cmath>
#include <stdexcept>

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

}  // namespace bt
