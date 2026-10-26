#pragma once
// Transaction-cost model. Every fill pays the spread, then slippage, then commission.
//
// Price basis says what the input quotes are:
//   Mid: buys fill at mid + spread/2 + slippage, sells at mid - spread/2 - slippage.
//   Bid: buys fill at the ask (bid + spread) + slippage, sells at bid - slippage. HistData FX bars are bid quotes.
// Spread and slippage are in price units (0.0001 = 1 pip on EURUSD). Commission is per unit traded plus an
// optional fraction of traded notional, both in account currency.

#include "bt/types.hpp"

namespace bt {

enum class PriceBasis { Mid, Bid };

struct CostModel {
    PriceBasis basis = PriceBasis::Mid;
    double spread = 0.0;
    double slippage = 0.0;
    double commission_per_unit = 0.0;
    double commission_rate = 0.0;
};

double execution_price(const CostModel& costs, Side side, double quote);
double commission(const CostModel& costs, double quantity, double price);

}  // namespace bt
