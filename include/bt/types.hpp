#pragma once
// Core value types shared by every part of the engine.

#include <cstdint>

namespace bt {

// One OHLC bar. `timestamp` is seconds since 1970-01-01 in the data's own clock: the loader does no
// time-zone conversion, so HistData bars stay in EST (no DST) exactly as supplied.
struct Bar {
    std::int64_t timestamp = 0;
    double open = 0.0;
    double high = 0.0;
    double low = 0.0;
    double close = 0.0;
};

enum class Side { Buy, Sell };

// A completed market order. `quantity` is always positive; `side` carries the direction.
struct Fill {
    std::int64_t timestamp = 0;
    Side side = Side::Buy;
    double quantity = 0.0;
    double price = 0.0;  // execution price after spread and slippage
    double fee = 0.0;    // commission, in account currency
};

inline double signed_quantity(const Fill& f) { return f.side == Side::Buy ? f.quantity : -f.quantity; }

// A target position decided at the close of the bar with this timestamp, e.g. by a model trained elsewhere.
struct TimedTarget {
    std::int64_t timestamp = 0;
    double target = 0.0;
};

}  // namespace bt
