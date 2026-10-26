"""Write the small synthetic data sets in examples/ (they let the tests run without any downloaded data).

examples/tiny.csv          3,000 hourly bars of a seeded random walk with slow trends, 5-decimal prices
examples/hand_example.csv  the 5-bar example worked out by hand in tests/test_engine.cpp
"""
from pathlib import Path

import numpy as np
import pandas as pd

EXAMPLES = Path(__file__).resolve().parents[1] / "examples"


def tiny(n: int = 3000, seed: int = 2024) -> pd.DataFrame:
    rng = np.random.default_rng(seed)
    drift = np.repeat(rng.normal(0.0, 0.0002, n // 250 + 1), 250)[:n]  # trends that last ~250 bars
    log_close = np.log(1.10) + np.cumsum(drift + rng.normal(0.0, 0.0012, n))
    close = np.round(np.exp(log_close), 5)
    gap = rng.normal(0.0, 0.0001, n)
    open_ = np.round(np.concatenate([[1.10], close[:-1]]) + gap, 5)
    wick = np.round(np.abs(rng.normal(0.0, 0.0006, (2, n))), 5)
    high = np.round(np.maximum(open_, close) + wick[0], 5)
    low = np.round(np.minimum(open_, close) - wick[1], 5)
    ts = pd.date_range("2023-01-02 00:00", periods=n, freq="1h")
    return pd.DataFrame({"timestamp": ts.strftime("%Y-%m-%d %H:%M:%S"), "open": open_, "high": high, "low": low, "close": close})


def hand_example() -> pd.DataFrame:
    o = [1.1000, 1.1010, 1.1030, 1.1020, 1.1000]
    c = [1.1010, 1.1030, 1.1020, 1.1000, 1.0980]
    ts = pd.date_range("2024-01-01 00:00", periods=5, freq="1h")
    return pd.DataFrame({"timestamp": ts.strftime("%Y-%m-%d %H:%M:%S"), "open": o,
                         "high": np.maximum(o, c), "low": np.minimum(o, c), "close": c})


if __name__ == "__main__":
    EXAMPLES.mkdir(exist_ok=True)
    tiny().to_csv(EXAMPLES / "tiny.csv", index=False, float_format="%.5f", lineterminator="\n")
    hand_example().to_csv(EXAMPLES / "hand_example.csv", index=False, float_format="%.5f", lineterminator="\n")
    print("wrote", EXAMPLES / "tiny.csv", "and", EXAMPLES / "hand_example.csv")
