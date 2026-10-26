"""Run every experiment reported in the README and write the numbers to results/.

  python python/run_experiments.py            # needs build/bt_run, build/bt_bench and data/ (see README)
  python python/make_figures.py               # charts from the saved numbers

Set-up shared by A-C: EURUSD hourly bid bars, 100,000 units per position, $100,000 starting capital, a fresh
account at the start of each period (indicators warm up on the bars before it), stop-out at zero equity.
Baseline costs: 0.5 pip spread on top of the bid quote plus $3.50 commission per side per 100,000 units.

  A  Cost sensitivity, development period 2004-2014.
  B  Parameter grid, short {5,10,20,30,50} x long {50,100,150,200,300}, on development and validation
     (2015-2019). Rule fixed in advance: the pair with the highest validation Sharpe is selected, and the choice is
     written to results/selection.json before the test period is run.
  C  The test period (2020-2024), run once for the selected pair, MA(20,50) and buy-and-hold. The full grid is
     then also run on the test period, after the selection, only to show where the selected pair ranked.
  D  Runtime on 1-minute bars: the C++ engine against the plain-Python loop and the NumPy reference.
"""
from __future__ import annotations

import json
import os
import platform
import subprocess
import sys
import tempfile
import time
from datetime import datetime, timezone
from pathlib import Path

import numpy as np
import pandas as pd

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "python"))
import reference_loop as loop  # noqa: E402
import reference_vectorised as ref  # noqa: E402

RESULTS = ROOT / "results"
CURVES = RESULTS / "curves"
HOURLY = ROOT / "data" / "eurusd_1h.csv"
MINUTE = ROOT / "data" / "eurusd_1m.csv"

PIP = 0.0001
COMMISSION = 3.5 / 100_000
UNITS, CASH = 100_000, 100_000
PERIODS = {"development": ("2004-01-01", "2015-01-01"), "validation": ("2015-01-01", "2020-01-01"),
           "test": ("2020-01-01", "2025-01-01")}
COST_LEVELS = {"No costs": (0.0, 0.0), "0.5 pip + commission": (0.5 * PIP, COMMISSION),
               "1 pip + commission": (1.0 * PIP, COMMISSION), "2 pips + commission": (2.0 * PIP, COMMISSION)}
BASELINE = "0.5 pip + commission"
SHORTS, LONGS = [5, 10, 20, 30, 50], [50, 100, 150, 200, 300]


def binary(name: str) -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    dirs = [Path(os.environ["BT_BUILD_DIR"])] if "BT_BUILD_DIR" in os.environ else []
    for d in dirs + [ROOT / "build", ROOT / "build" / "Release", ROOT / "cmake-build-release"]:
        if (d / f"{name}{suffix}").exists():
            return d / f"{name}{suffix}"
    raise SystemExit(f"{name} not found: build the Release configuration first "
                     "(cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build --config Release)")


BT_RUN, BT_BENCH = binary("bt_run"), binary("bt_bench")


def bt_run(strategy: str, period: str, cost: str, short: int = 20, long: int = 50, curve_name: str | None = None) -> dict:
    spread, commission = COST_LEVELS[cost]
    start, end = PERIODS[period]
    args = [str(BT_RUN), "--data", str(HOURLY), "--strategy", strategy, "--short", str(short), "--long", str(long),
            "--units", str(UNITS), "--cash", str(CASH), "--basis", "bid", "--spread", repr(spread),
            "--commission-per-unit", repr(commission), "--start", start, "--end", end]
    with tempfile.TemporaryDirectory() as tmp:
        if curve_name:
            args += ["--out", tmp]
        out = subprocess.run(args, capture_output=True, text=True, check=True)
        if curve_name:
            curve = pd.read_csv(Path(tmp) / "equity.csv", usecols=["timestamp", "equity"])
            curve.to_csv(CURVES / f"{curve_name}.csv", index=False)
    m = json.loads(out.stdout)
    m["round_trips_per_year"] = m["fills"] / 2 / m["years"]
    return m


def summary(m: dict, label: str, **extra) -> dict:
    keys = ["net_pnl", "annual_return_on_capital", "sharpe_pnl_zero_rf", "max_drawdown_on_capital", "fills",
            "round_trips_per_year", "fees_paid", "spread_slippage_cost", "exposure", "stopped_out", "stop_out_at",
            "final_equity", "cagr", "sharpe_zero_rf", "max_drawdown", "years"]
    return {"label": label, **extra, **{k: m.get(k) for k in keys}}


def experiment_a() -> list[dict]:
    rows = []
    for name, strategy, short, long in [("MA(20,50)", "ma", 20, 50), ("MA(50,200)", "ma", 50, 200),
                                        ("Buy and hold", "buyhold", 0, 1)]:
        for cost in COST_LEVELS:
            curve = f"A_{name}_{cost}".replace(" ", "_").replace("(", "").replace(")", "").replace(",", "-").replace("+", "plus")
            m = bt_run(strategy, "development", cost, short, long, curve_name=curve)
            rows.append(summary(m, name, cost=cost, curve=curve))
    return rows


def experiment_b() -> tuple[list[dict], dict]:
    rows = []
    for period in ("development", "validation"):
        for s in SHORTS:
            for l in LONGS:
                if s >= l:
                    continue
                m = bt_run("ma", period, BASELINE, s, l)
                rows.append(summary(m, f"MA({s},{l})", period=period, short=s, long=l))
    grid = pd.DataFrame(rows)
    val = grid[grid.period == "validation"].set_index(["short", "long"])["sharpe_pnl_zero_rf"]
    dev = grid[grid.period == "development"].set_index(["short", "long"])["sharpe_pnl_zero_rf"]
    best = val.idxmax()
    years_val = grid[grid.period == "validation"]["years"].iloc[0]
    # Yardstick for selection luck: expected best Sharpe among 24 independent strategies with no edge.
    rng = np.random.default_rng(0)
    expected_max_z = float(rng.standard_normal((200_000, len(val))).max(axis=1).mean())
    selection = {
        "rule": "highest validation-period Sharpe (P&L-based, zero risk-free rate) at baseline costs",
        "selected": {"short": int(best[0]), "long": int(best[1])},
        "validation_sharpe": float(val[best]),
        "development_sharpe": float(dev[best]),
        "candidates": int(len(val)),
        "validation_sharpe_rank_correlation_with_development": float(val.rank().corr(dev.rank())),
        "pairs_with_positive_validation_sharpe": int((val > 0).sum()),
        "pairs_with_positive_development_sharpe": int((dev > 0).sum()),
        "luck_yardstick": {
            "expected_best_of_n_sharpe_if_no_edge_and_independent": expected_max_z / np.sqrt(years_val),
            "note": "E[max of n standard normals] / sqrt(years): the Sharpe a best-of-n pick reaches by luck alone; "
                    "correlated variants lower it.",
        },
        "written_at_utc": datetime.now(timezone.utc).isoformat(timespec="seconds"),
    }
    (RESULTS / "selection.json").write_text(json.dumps(selection, indent=2))  # fixed before the test run
    return rows, selection


def experiment_c(selection: dict) -> tuple[list[dict], list[dict]]:
    s, l = selection["selected"]["short"], selection["selected"]["long"]
    rows = [summary(bt_run("ma", "test", BASELINE, s, l, curve_name="C_selected"), f"MA({s},{l})", role="selected on validation"),
            summary(bt_run("ma", "test", BASELINE, 20, 50, curve_name="C_ma20_50"), "MA(20,50)", role="default"),
            summary(bt_run("buyhold", "test", BASELINE, curve_name="C_buyhold"), "Buy and hold", role="benchmark")]
    after = []  # diagnosis only: run after the selection was written
    for ss in SHORTS:
        for ll in LONGS:
            if ss < ll:
                after.append(summary(bt_run("ma", "test", BASELINE, ss, ll), f"MA({ss},{ll})", short=ss, long=ll))
    return rows, after


def experiment_d() -> dict:
    sizes = [100_000, 1_000_000, None]
    out = {"machine": {"cpu": cpu_name(), "logical_cpus": os.cpu_count(), "os": platform.platform(),
                       "python": platform.python_version(), "numpy": np.__version__, "pandas": pd.__version__},
           "config": "MA(20,50), 100,000 units, bid quotes + 1 pip, $3.50/side commission, stop-out off, 1-minute EURUSD",
           "runs": []}
    for n in sizes:
        res = subprocess.run([str(BT_BENCH), "--data", str(MINUTE), "--repeat", "5", "--limit", str(n or 0)],
                             capture_output=True, text=True, check=True)
        b = json.loads(res.stdout)
        out["compiler"], out["build"] = b["compiler"], b["build"]
        out["runs"].append({"implementation": "C++ engine", "bars": b["bars"], "seconds": b["run_seconds_best"],
                            "seconds_median": b["run_seconds_median"], "load_seconds": b["load_seconds"],
                            "final_equity": b["final_equity"], "fills": b["fills"]})

    t0 = time.perf_counter()
    bars = ref.load_bars(MINUTE)
    py_load = time.perf_counter() - t0
    costs = ref.Costs("bid", PIP, 0.0, COMMISSION, 0.0)
    for n in sizes:
        k = n or len(bars)
        sub = ref.Bars(*(a[:k] for a in (bars.timestamp, bars.open, bars.high, bars.low, bars.close)))
        ts, op, cl = sub.timestamp.tolist(), sub.open.tolist(), sub.close.tolist()
        times, final, fills = [], None, None
        for _ in range(3 if k <= 1_000_000 else 1):
            t = time.perf_counter()
            r = loop.run(ts, op, cl, loop.MovingAverageCrossover(20, 50, float(UNITS)), basis="bid", spread=PIP,
                         commission_per_unit=COMMISSION, initial_cash=float(CASH), stop_out_equity=-np.inf)
            times.append(time.perf_counter() - t)
            final, fills = r["curve"][-1][4], len(r["fills"])
            del r
        out["runs"].append({"implementation": "Python loop", "bars": k, "seconds": min(times),
                            "seconds_median": float(np.median(times)), "final_equity": final, "fills": fills})
        times = []
        for _ in range(5):
            t = time.perf_counter()
            target = ref.ma_crossover_targets(sub.close, 20, 50, float(UNITS), method="rolling")
            v = ref.backtest(sub, target, costs, float(CASH), stop_out_equity=-np.inf)
            times.append(time.perf_counter() - t)
        out["runs"].append({"implementation": "NumPy (vectorised)", "bars": k, "seconds": min(times),
                            "seconds_median": float(np.median(times)), "final_equity": float(v.curve["equity"].iloc[-1]),
                            "fills": len(v.fills)})
        del ts, op, cl
    out["python_load_seconds"] = py_load
    return out


def cpu_name() -> str:
    try:
        for line in Path("/proc/cpuinfo").read_text().splitlines():
            if line.startswith("model name"):
                return line.split(":", 1)[1].strip()
    except OSError:
        pass
    return platform.processor() or "unknown"


def main() -> None:
    for p in (HOURLY, MINUTE):
        if not p.exists():
            raise SystemExit(f"missing {p}: run python/prepare_data.py first")
    CURVES.mkdir(parents=True, exist_ok=True)
    report = {"setup": {"units": UNITS, "starting_capital": CASH, "price_basis": "bid quotes; buys pay the spread",
                        "baseline_costs": BASELINE, "commission_per_side_per_100k": 3.5, "periods": PERIODS,
                        "stop_out": "equity <= 0 at a close -> flat at the next open, no further trades"}}
    report["A_costs"] = experiment_a()
    print("A done")
    report["B_grid"], report["selection"] = experiment_b()
    print("B done:", report["selection"]["selected"])
    report["C_test"], report["C_test_grid_after_selection"] = experiment_c(report["selection"])
    print("C done")
    if "--skip-runtime" not in sys.argv:
        report["D_runtime"] = experiment_d()
        print("D done")
    else:
        old = RESULTS / "experiments.json"
        if old.exists() and "D_runtime" in json.loads(old.read_text()):
            report["D_runtime"] = json.loads(old.read_text())["D_runtime"]
    (RESULTS / "experiments.json").write_text(json.dumps(report, indent=2, default=float))
    print("wrote", RESULTS / "experiments.json")


if __name__ == "__main__":
    main()
