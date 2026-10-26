"""Turn HistData EURUSD 1-minute bid bars into the CSV files the engine reads.

Input, one of:
  --pickle FILE     a pandas DataFrame with a DatetimeIndex and open/high/low/close columns
  --histdata DIR    HistData "Generic ASCII" M1 files (DAT_ASCII_EURUSD_M1_YYYY.csv: ';'-separated
                    'YYYYMMDD HHMMSS;open;high;low;close;volume', Eastern Standard Time without daylight saving)

Output, in --out (default: data/ next to this folder):
  eurusd_1m.csv      every 1-minute bar as given; HistData leaves out minutes with no ticks and nothing is filled
  eurusd_1h.csv      hourly bars: first open, highest high, lowest low and last close of the minutes in each
                     clock hour; hours with no minutes are dropped, not filled
  data_quality.json  row counts and the result of every check below

The script stops without writing bars if any check fails: timestamps sorted and unique, no missing values,
prices positive, high >= open/close/low, low <= open/close/high, no price with more than 5 decimals.

  python python/prepare_data.py --pickle HD_EURUSD_1min_2004-2024.pkl
"""
from __future__ import annotations

import argparse
import json
from pathlib import Path

import numpy as np
import pandas as pd

COLUMNS = ["open", "high", "low", "close"]


def read_pickle(path: Path) -> pd.DataFrame:
    df = pd.read_pickle(path)
    if not isinstance(df.index, pd.DatetimeIndex):
        raise SystemExit(f"{path}: expected a DataFrame with a DatetimeIndex")
    missing = [c for c in COLUMNS if c not in df.columns]
    if missing:
        raise SystemExit(f"{path}: missing column(s) {missing}")
    return df[COLUMNS].astype("float64")


def read_histdata(directory: Path) -> pd.DataFrame:
    files = sorted(directory.glob("DAT_ASCII_EURUSD_M1_*.csv"))
    if not files:
        raise SystemExit(f"no DAT_ASCII_EURUSD_M1_*.csv files in {directory}")
    parts = [pd.read_csv(f, sep=";", header=None, names=["time", *COLUMNS, "volume"], dtype={"time": str}) for f in files]
    df = pd.concat(parts, ignore_index=True)
    df.index = pd.DatetimeIndex(pd.to_datetime(df.pop("time"), format="%Y%m%d %H%M%S"), name="time")
    return df[COLUMNS].astype("float64")


def quality(df: pd.DataFrame) -> dict:
    scaled = df.to_numpy() * 1e5
    return {
        "rows": int(len(df)),
        "first_bar": str(df.index[0]),
        "last_bar": str(df.index[-1]),
        "sorted": bool(df.index.is_monotonic_increasing),
        "duplicate_timestamps": int(df.index.duplicated().sum()),
        "rows_with_missing_values": int(df.isna().any(axis=1).sum()),
        "rows_with_non_positive_prices": int((df <= 0).any(axis=1).sum()),
        "rows_with_high_below_open_close_or_low": int((df["high"] < df[["open", "close", "low"]].max(axis=1)).sum()),
        "rows_with_low_above_open_close_or_high": int((df["low"] > df[["open", "close", "high"]].min(axis=1)).sum()),
        "prices_with_more_than_5_decimals": int((np.abs(scaled - np.round(scaled)) > 1e-6).sum()),
    }


def passed(report: dict) -> bool:
    return report["sorted"] and not any(v for k, v in report.items() if k.startswith(("duplicate", "rows_with", "prices_with")))


def hourly(df: pd.DataFrame) -> pd.DataFrame:
    agg = df.resample("1h", label="left", closed="left").agg({"open": "first", "high": "max", "low": "min", "close": "last"})
    return agg.dropna()


def write_bars(df: pd.DataFrame, path: Path) -> None:
    out = df.copy()
    out.index = out.index.strftime("%Y-%m-%d %H:%M:%S")
    out.index.name = "timestamp"
    out.to_csv(path, float_format="%.5f", lineterminator="\n")


def main() -> None:
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    src = p.add_mutually_exclusive_group(required=True)
    src.add_argument("--pickle", type=Path)
    src.add_argument("--histdata", type=Path)
    p.add_argument("--out", type=Path, default=Path(__file__).resolve().parents[1] / "data")
    args = p.parse_args()

    minutes = read_pickle(args.pickle) if args.pickle else read_histdata(args.histdata)
    report = {"source": str(args.pickle or args.histdata), "quote_side": "bid", "time_zone": "EST, no daylight saving",
              "minute_bars": quality(minutes)}
    args.out.mkdir(parents=True, exist_ok=True)
    if not passed(report["minute_bars"]):
        (args.out / "data_quality.json").write_text(json.dumps(report, indent=2))
        raise SystemExit("data checks failed; see data_quality.json")

    hours = hourly(minutes)
    report["hourly_bars"] = quality(hours)
    write_bars(minutes, args.out / "eurusd_1m.csv")
    write_bars(hours, args.out / "eurusd_1h.csv")
    (args.out / "data_quality.json").write_text(json.dumps(report, indent=2))
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
