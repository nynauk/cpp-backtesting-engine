#include "bt/portfolio.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "bt/costs.hpp"

namespace bt {

double execution_price(const CostModel& c, Side side, double quote) {
    if (c.basis == PriceBasis::Mid) {
        const double half = 0.5 * c.spread;
        return side == Side::Buy ? quote + half + c.slippage : quote - half - c.slippage;
    }
    // Bid quotes: buying pays the ask, selling receives the bid.
    return side == Side::Buy ? quote + c.spread + c.slippage : quote - c.slippage;
}

double commission(const CostModel& c, double quantity, double price) {
    return quantity * c.commission_per_unit + quantity * price * c.commission_rate;
}

Portfolio::Portfolio(double initial_cash) : initial_cash_(initial_cash), cash_(initial_cash), equity_(initial_cash) {
    if (!(initial_cash > 0.0)) throw std::invalid_argument("initial cash must be positive");
}

void Portfolio::apply_fill(const Fill& f) {
    if (!(f.quantity > 0.0)) throw std::invalid_argument("fill quantity must be positive");
    if (!(f.price > 0.0)) throw std::invalid_argument("fill price must be positive");
    if (f.fee < 0.0) throw std::invalid_argument("fee cannot be negative");

    const double dq = signed_quantity(f);
    cash_ -= dq * f.price;
    cash_ -= f.fee;
    fees_paid_ += f.fee;

    const double old = position_;
    const double now = old + dq;
    if (old == 0.0 || (old > 0.0) == (dq > 0.0)) {
        // Opening or adding in the same direction: weighted average entry price.
        average_price_ = (std::abs(old) * average_price_ + f.quantity * f.price) / std::abs(now);
    } else {
        // Reducing, closing or flipping: realise P&L on the closed part against the average price.
        const double closed = std::min(f.quantity, std::abs(old));
        const double direction = old > 0.0 ? 1.0 : -1.0;
        realised_pnl_ += closed * (f.price - average_price_) * direction;
        if (std::abs(now) < 1e-12) {
            average_price_ = 0.0;
        } else if ((now > 0.0) != (old > 0.0)) {
            average_price_ = f.price;  // flipped: the remainder opens at this fill
        }
    }
    position_ = std::abs(now) < 1e-12 ? 0.0 : now;
    if (last_mark_ > 0.0) mark_to_market(last_mark_);
    else equity_ = cash_ + position_ * f.price;
}

void Portfolio::mark_to_market(double price) {
    if (!(price > 0.0)) throw std::invalid_argument("mark price must be positive");
    last_mark_ = price;
    equity_ = cash_ + position_ * price;
    unrealised_pnl_ = position_ == 0.0 ? 0.0 : position_ * (price - average_price_);
}

}  // namespace bt
