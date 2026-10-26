#pragma once
// Single-instrument portfolio with average-cost accounting.
//
// Invariant (checked in the tests): equity == initial_cash + realised_pnl + unrealised_pnl - fees_paid.
// Not modelled in this version: contract multipliers, leverage or margin limits (the engine's stop-out is the
// only account-level check), short-sale constraints, financing/swap, currency conversion. For EURUSD with a USD
// account no conversion is needed.

#include "bt/types.hpp"

namespace bt {

class Portfolio {
public:
    explicit Portfolio(double initial_cash);

    // Cash moves by -signed_quantity * price - fee. Position, average price and realised P&L follow the
    // average-cost method: adding keeps a weighted average, reducing realises P&L against it, and a flip
    // closes the old position before opening the remainder at the fill price.
    void apply_fill(const Fill& fill);

    // Revalues the open position. Changes equity and unrealised P&L, never cash.
    void mark_to_market(double price);

    double initial_cash() const { return initial_cash_; }
    double cash() const { return cash_; }
    double position() const { return position_; }
    double average_price() const { return average_price_; }
    double realised_pnl() const { return realised_pnl_; }
    double unrealised_pnl() const { return unrealised_pnl_; }
    double fees_paid() const { return fees_paid_; }
    double equity() const { return equity_; }
    double last_mark() const { return last_mark_; }

private:
    double initial_cash_;
    double cash_;
    double position_ = 0.0;
    double average_price_ = 0.0;
    double realised_pnl_ = 0.0;
    double unrealised_pnl_ = 0.0;
    double fees_paid_ = 0.0;
    double equity_;
    double last_mark_ = 0.0;
};

}  // namespace bt
