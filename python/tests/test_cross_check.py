"""The C++ engine, the NumPy reference and the plain-Python loop must agree on every fill and every equity point.

Runs on the synthetic examples/ data always, and on real EURUSD hourly bars when data/eurusd_1h.csv exists
(create it with python/prepare_data.py). Build the engine first: cmake -S . -B build && cmake --build build.
"""
import json
import math
import os
import subprocess
from pathlib import Path

import numpy as np
import pandas as pd
import pytest

import reference_loop as loop
import reference_vectorised as ref
from conftest import ROOT, find_binary

BT_RUN = find_binary("bt_run")
TINY = ROOT / "examples" / "tiny.csv"
HAND = ROOT / "examples" / "hand_example.csv"
HOURLY = Path(os.environ.get("BT_HOURLY_DATA", ROOT / "data" / "eurusd_1h.csv"))
needs_engine = pytest.mark.skipif(BT_RUN is None, reason="C++ engine not built")
needs_data = pytest.mark.skipif(not HOURLY.exists(), reason="no data/eurusd_1h.csv (run python/prepare_data.py)")

PIP = 0.0001
COMMISSION = 3.5 / 100_000  # $3.50 per side per 100,000 units
COMPARED = ["observations", "final_equity", "ruined", "stopped_out", "total_return", "cagr", "annual_volatility",
            "sharpe_zero_rf", "max_drawdown", "net_pnl", "annual_return_on_capital", "annual_volatility_on_capital",
            "sharpe_pnl_zero_rf", "max_drawdown_on_capital", "fills", "exposure", "turnover_per_year", "fees_paid",
            "spread_slippage_cost"]


def run_cpp(data: Path, out: Path, strategy="ma", short=20, long=50, units=100_000, cash=100_000, basis="mid",
            spread=0.0, slippage=0.0, commission_per_unit=0.0, commission_rate=0.0, start=None, end=None, stop_out="0"):
    args = [str(BT_RUN), "--data", str(data), "--strategy", strategy, "--short", str(short), "--long", str(long),
            "--units", repr(float(units)), "--cash", repr(float(cash)), "--basis", basis, "--spread", repr(spread),
            "--slippage", repr(slippage), "--commission-per-unit", repr(commission_per_unit),
            "--commission-rate", repr(commission_rate), "--stop-out", stop_out, "--out", str(out)]
    if start:
        args += ["--start", start]
    if end:
        args += ["--end", end]
    done = subprocess.run(args, capture_output=True, text=True, check=True)
    metrics = json.loads(done.stdout)
    curve = pd.read_csv(out / "equity.csv")
    fills = pd.read_csv(out / "fills.csv")
    return metrics, curve, fills


def run_reference(data: Path, strategy="ma", short=20, long=50, units=100_000, cash=100_000, basis="mid", spread=0.0,
                  slippage=0.0, commission_per_unit=0.0, commission_rate=0.0, start=None, end=None, stop_out="0"):
    bars = ref.load_bars(data)
    if strategy == "ma":
        target = ref.ma_crossover_targets(bars.close, short, long, units)
    else:
        target = np.full(len(bars), float(units) if strategy == "buyhold" else 0.0)
    costs = ref.Costs(basis, spread, slippage, commission_per_unit, commission_rate)
    level = -math.inf if stop_out == "none" else float(stop_out)
    result = ref.backtest(bars, target, costs, float(cash), ref.to_seconds(start) if start else None,
                          ref.to_seconds(end) if end else None, level)
    return ref.metrics(result), result


def same_number(a, b, rel=1e-8, abs_=1e-9):
    if a is None or (isinstance(a, float) and math.isnan(a)):
        return b is None or (isinstance(b, float) and math.isnan(b))
    if isinstance(a, bool) or isinstance(b, bool):
        return bool(a) == bool(b)
    return math.isclose(float(a), float(b), rel_tol=rel, abs_tol=abs_)


def assert_agree(cpp, reference):
    cpp_metrics, cpp_curve, cpp_fills = cpp
    ref_metrics, result = reference
    ref_fills, ref_curve = result.fills, result.curve

    assert len(cpp_fills) == len(ref_fills), "different number of fills"
    if len(ref_fills):
        cpp_ts = pd.to_datetime(cpp_fills["timestamp"]).to_numpy().astype("datetime64[s]").astype(np.int64)
        np.testing.assert_array_equal(cpp_ts, ref_fills["timestamp"].to_numpy())
        np.testing.assert_array_equal(cpp_fills["side"].to_numpy(), ref_fills["side"].to_numpy())
        np.testing.assert_allclose(cpp_fills["quantity"], ref_fills["quantity"], rtol=0, atol=1e-6)
        np.testing.assert_allclose(cpp_fills["price"], ref_fills["price"], rtol=0, atol=1e-10)
        np.testing.assert_allclose(cpp_fills["fee"], ref_fills["fee"], rtol=0, atol=1e-8)

    assert len(cpp_curve) == len(ref_curve), "different number of equity points"
    np.testing.assert_allclose(cpp_curve["position"], ref_curve["position"], rtol=0, atol=1e-6)
    np.testing.assert_allclose(cpp_curve["equity"], ref_curve["equity"], rtol=0, atol=1e-5)  # dollars

    for key in COMPARED:
        assert same_number(cpp_metrics.get(key), ref_metrics.get(key)), f"{key}: C++ {cpp_metrics.get(key)} vs reference {ref_metrics.get(key)}"
    if result.stopped_out:
        assert cpp_metrics["stop_out_at"] == pd.Timestamp(result.stop_out_timestamp, unit="s").strftime("%Y-%m-%d %H:%M:%S")


def test_reference_matches_the_hand_calculation():
    # Same worked example as tests/test_engine.cpp (MatchesAHandCalculatedPortfolio).
    bars = ref.load_bars(HAND)
    costs = ref.Costs("mid", spread=0.0002, commission_per_unit=0.00001)
    result = ref.backtest(bars, [1000, 1000, 0, -1000, -1000], costs)
    np.testing.assert_allclose(result.fills["price"], [1.1011, 1.1019, 1.0999], atol=1e-12)
    np.testing.assert_allclose(result.curve["equity"], [100000.00, 100001.89, 100000.89, 100000.78, 100002.67], atol=1e-8)
    assert result.fees_paid == pytest.approx(0.03, abs=1e-12)


def test_window_and_rolling_moving_averages_give_the_same_targets():
    close = ref.load_bars(TINY).close
    for short, long in [(5, 20), (10, 50), (3, 7)]:
        np.testing.assert_array_equal(ref.ma_crossover_targets(close, short, long, 1.0, "window"),
                                      ref.ma_crossover_targets(close, short, long, 1.0, "rolling"))


TINY_CASES = {
    "mid, spread, slippage, per-unit commission": dict(short=5, long=20, units=1000, basis="mid", spread=0.0002,
                                                       slippage=0.00005, commission_per_unit=0.00001),
    "bid, spread, commission on notional, window": dict(short=3, long=10, basis="bid", spread=PIP, commission_rate=0.00002,
                                                        start="2023-02-01", end="2023-05-01"),
    "stop-out with a small account": dict(short=5, long=20, cash=2000, basis="bid", spread=2 * PIP,
                                          commission_per_unit=COMMISSION),
    "stop-out switched off": dict(short=5, long=20, cash=2000, basis="bid", spread=2 * PIP, stop_out="none"),
    "buy and hold": dict(strategy="buyhold", basis="bid", spread=PIP),
    "no trades": dict(strategy="none"),
}


@needs_engine
@pytest.mark.parametrize("case", TINY_CASES, ids=list(TINY_CASES))
def test_engine_matches_reference_on_synthetic_data(case, tmp_path):
    kwargs = TINY_CASES[case]
    assert_agree(run_cpp(TINY, tmp_path, **kwargs), run_reference(TINY, **kwargs))


def test_plain_python_loop_matches_the_vectorised_reference():
    bars = ref.load_bars(TINY)
    kwargs = dict(basis="bid", spread=PIP, commission_per_unit=COMMISSION)
    for cash in (100_000.0, 2000.0):  # the second one is stopped out
        out = loop.run(bars.timestamp.tolist(), bars.open.tolist(), bars.close.tolist(),
                       loop.MovingAverageCrossover(5, 20, 100_000.0), initial_cash=cash, **kwargs)
        vec = ref.backtest(bars, ref.ma_crossover_targets(bars.close, 5, 20, 100_000.0), ref.Costs(**kwargs), cash)
        np.testing.assert_allclose([p[4] for p in out["curve"]], vec.curve["equity"], rtol=0, atol=1e-5)
        assert len(out["fills"]) == len(vec.fills)
        assert (out["stop_out_at"] is not None) == vec.stopped_out


REAL_CASES = {
    "2019, bid, 0.5 pip + commission": dict(short=20, long=50, basis="bid", spread=0.5 * PIP,
                                            commission_per_unit=COMMISSION, start="2019-01-01", end="2020-01-01"),
    "2004-2014, mid, no costs, long windows": dict(short=5, long=300, start="2004-01-01", end="2015-01-01"),
    "2004-2024, bid, 1 pip + commission (stops out)": dict(short=20, long=50, basis="bid", spread=PIP,
                                                           commission_per_unit=COMMISSION),
    "2015-2019, bid, 2 pips, slippage": dict(short=50, long=200, basis="bid", spread=2 * PIP, slippage=0.2 * PIP,
                                             commission_per_unit=COMMISSION, start="2015-01-01", end="2020-01-01"),
}


@needs_engine
@needs_data
@pytest.mark.parametrize("case", REAL_CASES, ids=list(REAL_CASES))
def test_engine_matches_reference_on_eurusd(case, tmp_path):
    kwargs = REAL_CASES[case]
    assert_agree(run_cpp(HOURLY, tmp_path, **kwargs), run_reference(HOURLY, **kwargs))
