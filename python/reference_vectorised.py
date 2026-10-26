"""Independent NumPy implementation of the engine's conventions, used to cross-check the C++ results.

Nothing here is translated from the C++ code. Moving averages are computed window by window (or with pandas
rolling means) instead of running sums, and positions, fills, cash and equity are computed as whole arrays
instead of in an event loop. When two implementations built this differently agree to rounding error, both
are very likely to implement the stated conventions.

Conventions (the same as the engine):
  * a target set after bar t closes is filled at bar t+1's open; equity is marked at every close;
  * equity is recorded for bars in [start, end); earlier bars only warm up the indicators;
  * mid quotes: buys pay half the spread plus slippage, sells receive half the spread less slippage;
    bid quotes: buys pay the full spread plus slippage, sells give up only slippage;
  * commission = units * per-unit charge + notional * rate;
  * stop-out: equity at a close at or below the level -> flat at the next open, no orders afterwards.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
import pandas as pd
from numpy.lib.stride_tricks import sliding_window_view

SECONDS_PER_YEAR = 365.25 * 86400.0


@dataclass(frozen=True)
class Costs:
    basis: str = "mid"  # "mid" or "bid"
    spread: float = 0.0
    slippage: float = 0.0
    commission_per_unit: float = 0.0
    commission_rate: float = 0.0


@dataclass
class Bars:
    timestamp: np.ndarray  # int64 seconds since 1970-01-01, wall-clock time of the data (no time zone)
    open: np.ndarray
    high: np.ndarray
    low: np.ndarray
    close: np.ndarray

    def __len__(self) -> int:
        return len(self.timestamp)


@dataclass
class Result:
    curve: pd.DataFrame  # timestamp, close, position, cash, equity
    fills: pd.DataFrame  # timestamp, side, quantity, price, fee
    initial_cash: float
    fees_paid: float
    traded_notional: float
    spread_slippage_cost: float
    stopped_out: bool = False
    stop_out_timestamp: int | None = None
    extra: dict = field(default_factory=dict)


def load_bars(path: str | Path) -> Bars:
    df = pd.read_csv(path)
    ts = pd.to_datetime(df["timestamp"]).to_numpy().astype("datetime64[s]").astype(np.int64)
    return Bars(ts, *(df[c].to_numpy(np.float64) for c in ("open", "high", "low", "close")))


def to_seconds(text: str) -> int:
    return int(np.datetime64(pd.Timestamp(text).to_datetime64(), "s").astype(np.int64))


def moving_average(values: np.ndarray, window: int, method: str = "window") -> np.ndarray:
    """Mean of values[t-window+1 .. t] for t = window-1 .. n-1."""
    if method == "window":  # each window summed on its own: no running-sum drift at all
        return sliding_window_view(values, window).mean(axis=1)
    if method == "rolling":  # pandas' O(n) rolling mean, for speed
        return pd.Series(values).rolling(window).mean().to_numpy()[window - 1:]
    raise ValueError(f"unknown method {method!r}")


def ma_crossover_targets(close: np.ndarray, short: int, long: int, units: float, method: str = "window") -> np.ndarray:
    """Target position decided at each close: +units / -units by the sign of short MA - long MA, 0 in warm-up.
    A difference within 1e-12 of the long MA keeps the previous side (flat if there is none yet)."""
    if not 0 < short < long:
        raise ValueError("need 0 < short < long")
    n = len(close)
    target = np.zeros(n)
    if n < long:
        return target
    long_ma = moving_average(close, long, method)
    short_ma = moving_average(close, short, method)[long - short:]
    diff = short_ma - long_ma
    tolerance = 1e-12 * np.abs(long_ma)
    side = np.full(diff.shape, np.nan)
    side[diff > tolerance] = 1.0
    side[diff < -tolerance] = -1.0
    side = pd.Series(side).ffill().fillna(0.0).to_numpy()
    target[long - 1:] = side * units
    return target


def execution_prices(costs: Costs, quotes: np.ndarray, buying: np.ndarray) -> np.ndarray:
    if costs.basis == "mid":
        half = 0.5 * costs.spread
        return np.where(buying, quotes + half + costs.slippage, quotes - half - costs.slippage)
    if costs.basis == "bid":
        return np.where(buying, quotes + costs.spread + costs.slippage, quotes - costs.slippage)
    raise ValueError(f"unknown price basis {costs.basis!r}")


def _simulate(bars: Bars, target: np.ndarray, costs: Costs, initial_cash: float, lo: int, hi: int):
    position = np.zeros(hi - lo)
    first = max(lo, 1)  # the position during bar t is the target set at bar t-1's close
    position[first - lo:] = target[first - 1:hi - 1]
    delta = np.diff(position, prepend=0.0)  # flat before the window opens
    quotes = bars.open[lo:hi]
    price = execution_prices(costs, quotes, delta > 0)
    quantity = np.abs(delta)
    fee = quantity * costs.commission_per_unit + quantity * price * costs.commission_rate
    cash = initial_cash - np.cumsum(delta * price + fee)
    equity = cash + position * bars.close[lo:hi]
    return position, delta, quotes, price, fee, cash, equity


def backtest(bars: Bars, target: np.ndarray, costs: Costs = Costs(), initial_cash: float = 100_000.0,
             start: int | None = None, end: int | None = None, stop_out_equity: float = 0.0) -> Result:
    if not stop_out_equity < initial_cash:
        raise ValueError("stop-out level must be below the initial cash")
    lo = 0 if start is None else int(np.searchsorted(bars.timestamp, start, side="left"))
    hi = len(bars) if end is None else int(np.searchsorted(bars.timestamp, end, side="left"))
    target = np.array(target, dtype=np.float64)
    position, delta, quotes, price, fee, cash, equity = _simulate(bars, target, costs, initial_cash, lo, hi)

    stop_out_timestamp = None
    breach = np.flatnonzero(equity <= stop_out_equity)
    if breach.size:
        k = lo + int(breach[0])
        stop_out_timestamp = int(bars.timestamp[k])
        target[k:] = 0.0  # flat at the next open, then no new orders
        position, delta, quotes, price, fee, cash, equity = _simulate(bars, target, costs, initial_cash, lo, hi)

    traded = delta != 0.0
    ts = bars.timestamp[lo:hi]
    fills = pd.DataFrame({"timestamp": ts[traded], "side": np.where(delta[traded] > 0, "BUY", "SELL"),
                          "quantity": np.abs(delta[traded]), "price": price[traded], "fee": fee[traded]})
    curve = pd.DataFrame({"timestamp": ts, "close": bars.close[lo:hi], "position": position, "cash": cash, "equity": equity})
    return Result(curve, fills, initial_cash, float(fee.sum()), float((np.abs(delta) * price).sum()),
                  float((np.abs(delta) * np.abs(price - quotes)).sum()), stop_out_timestamp is not None, stop_out_timestamp)


def metrics(r: Result) -> dict:
    """Same definitions as the engine's metrics.hpp, computed with whole-array NumPy operations."""
    e = r.curve["equity"].to_numpy()
    n = len(e)
    out = {"observations": n, "fills": len(r.fills), "fees_paid": r.fees_paid,
           "spread_slippage_cost": r.spread_slippage_cost, "stopped_out": r.stopped_out}
    if n == 0:
        return out | {"final_equity": r.initial_cash}
    prev = np.concatenate([[r.initial_cash], e[:-1]])
    ruined = bool((e <= 0).any())
    with np.errstate(divide="ignore", invalid="ignore"):
        returns = np.where(prev > 0, e / prev - 1.0, 0.0)
    pnl = e - prev
    peak = np.maximum.accumulate(np.concatenate([[r.initial_cash], e]))[1:]
    ts = r.curve["timestamp"].to_numpy()
    years = (ts[-1] - ts[0]) / SECONDS_PER_YEAR
    ppy = n / years if years > 0 else np.nan
    sd_r, sd_p = (returns.std(ddof=1), pnl.std(ddof=1)) if n > 1 else (0.0, 0.0)
    nan = float("nan")
    out |= {
        "years": years,
        "periods_per_year": ppy,
        "final_equity": e[-1],
        "ruined": ruined,
        "total_return": e[-1] / r.initial_cash - 1.0,
        "max_drawdown": max(min(0.0, float((e / peak - 1.0).min())), -1.0),
        "net_pnl": e[-1] - r.initial_cash,
        "max_drawdown_on_capital": min(0.0, float(((e - peak) / r.initial_cash).min())),
        "exposure": float((r.curve["position"].to_numpy() != 0).mean()),
        "annual_return_on_capital": pnl.mean() * ppy / r.initial_cash,
        "annual_volatility_on_capital": sd_p * np.sqrt(ppy) / r.initial_cash,
        "sharpe_pnl_zero_rf": pnl.mean() / sd_p * np.sqrt(ppy) if sd_p > 0 else nan,
        "turnover_per_year": r.traded_notional / r.initial_cash / years if years > 0 else nan,
        "cagr": nan if ruined else (e[-1] / r.initial_cash) ** (1.0 / years) - 1.0,
        "annual_volatility": nan if ruined else sd_r * np.sqrt(ppy),
        "sharpe_zero_rf": nan if (ruined or sd_r == 0) else returns.mean() / sd_r * np.sqrt(ppy),
    }
    return out
