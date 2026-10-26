"""Plain-Python event loop with the engine's exact conventions, bar by bar.

This is the comparison point for the runtime benchmark: the same algorithm as the C++ engine (running-sum
moving averages, average-cost portfolio, fill at the next open, stop-out), written the way a first Python
backtester usually is. It is also checked against the C++ engine and the NumPy reference in the tests.
"""
from __future__ import annotations

BUY, SELL = 1, -1


class Portfolio:
    __slots__ = ("cash", "position", "average_price", "realised_pnl", "fees_paid")

    def __init__(self, cash: float):
        self.cash, self.position, self.average_price, self.realised_pnl, self.fees_paid = cash, 0.0, 0.0, 0.0, 0.0

    def apply_fill(self, side: int, quantity: float, price: float, fee: float) -> None:
        dq = quantity if side == BUY else -quantity
        self.cash -= dq * price
        self.cash -= fee
        self.fees_paid += fee
        old = self.position
        now = old + dq
        if old == 0.0 or (old > 0.0) == (dq > 0.0):
            self.average_price = (abs(old) * self.average_price + quantity * price) / abs(now)
        else:
            closed = min(quantity, abs(old))
            self.realised_pnl += closed * (price - self.average_price) * (1.0 if old > 0.0 else -1.0)
            if abs(now) < 1e-12:
                self.average_price = 0.0
            elif (now > 0.0) != (old > 0.0):
                self.average_price = price
        self.position = 0.0 if abs(now) < 1e-12 else now


class MovingAverageCrossover:
    def __init__(self, short: int, long: int, units: float):
        if not 0 < short < long:
            raise ValueError("need 0 < short < long")
        self.short, self.long, self.units = short, long, units
        self.short_sum = self.long_sum = self.last_target = 0.0

    def target(self, closes: list, t: int) -> float:
        short, long = self.short, self.long
        if t % 4096 == 0:  # resync the running sums, as the engine does
            self.short_sum = sum(closes[max(0, t + 1 - short):t + 1])
            self.long_sum = sum(closes[max(0, t + 1 - long):t + 1])
        else:
            c = closes[t]
            self.short_sum += c
            self.long_sum += c
            if t >= short:
                self.short_sum -= closes[t - short]
            if t >= long:
                self.long_sum -= closes[t - long]
        if t + 1 < long:
            return 0.0
        short_ma = self.short_sum / short
        long_ma = self.long_sum / long
        tolerance = 1e-12 * abs(long_ma)
        if short_ma - long_ma > tolerance:
            self.last_target = self.units
        elif long_ma - short_ma > tolerance:
            self.last_target = -self.units
        return self.last_target


def run(timestamps: list, opens: list, closes: list, strategy, basis: str = "mid", spread: float = 0.0,
        slippage: float = 0.0, commission_per_unit: float = 0.0, commission_rate: float = 0.0,
        initial_cash: float = 100_000.0, start: int | None = None, end: int | None = None,
        stop_out_equity: float = 0.0) -> dict:
    half = 0.5 * spread
    buy_add, sell_sub = (half + slippage, half + slippage) if basis == "mid" else (spread + slippage, slippage)
    port = Portfolio(initial_cash)
    curve, fills = [], []
    pending, have_pending, liquidating, halted, stop_out_at = 0.0, False, False, False, None
    traded_notional = spread_cost = 0.0
    for t in range(len(timestamps)):
        stamp = timestamps[t]
        if end is not None and stamp >= end:
            break
        in_window = start is None or stamp >= start
        if in_window and (have_pending or liquidating):
            delta = (0.0 if liquidating else pending) - port.position
            if abs(delta) > 1e-9:
                quote = opens[t]
                side = BUY if delta > 0.0 else SELL
                quantity = abs(delta)
                price = quote + buy_add if side == BUY else quote - sell_sub
                fee = quantity * commission_per_unit + quantity * price * commission_rate
                port.apply_fill(side, quantity, price, fee)
                fills.append((stamp, side, quantity, price, fee))
                traded_notional += quantity * price
                spread_cost += quantity * abs(price - quote)
            if liquidating:
                liquidating, halted, have_pending = False, True, False
        if in_window:
            close = closes[t]
            equity = port.cash + port.position * close
            curve.append((stamp, close, port.position, port.cash, equity))
            if not halted and not liquidating and equity <= stop_out_equity:
                stop_out_at = stamp
                if port.position != 0.0:
                    liquidating = True
                else:
                    halted = True
                have_pending = False
        if not halted and not liquidating:
            pending = strategy.target(closes, t)
            have_pending = True
    return {"curve": curve, "fills": fills, "fees_paid": port.fees_paid, "realised_pnl": port.realised_pnl,
            "traded_notional": traded_notional, "spread_slippage_cost": spread_cost, "stop_out_at": stop_out_at}
