"""Draw the README figures from results/experiments.json and results/curves/ (run run_experiments.py first).

Each figure is written twice, results/figures/<name>_light.png and <name>_dark.png; the README picks one with
<picture> to match the reader's GitHub theme. Colours come from one validated palette: categorical slots for
distinct strategies, a one-hue ordinal ramp for the ordered cost levels, blue-grey-red for signed Sharpe ratios.
"""
from __future__ import annotations

import json

import matplotlib

matplotlib.use("Agg")
import matplotlib.dates as mdates  # noqa: E402
import matplotlib.pyplot as plt  # noqa: E402
import numpy as np  # noqa: E402
import pandas as pd  # noqa: E402
from matplotlib.colors import LinearSegmentedColormap, TwoSlopeNorm, to_rgb  # noqa: E402
from matplotlib.patches import PathPatch, Rectangle  # noqa: E402
from matplotlib.path import Path  # noqa: E402

import pathlib  # noqa: E402

ROOT = pathlib.Path(__file__).resolve().parents[1]
RESULTS = ROOT / "results"
FIGURES = RESULTS / "figures"
PX = 0.75  # one CSS pixel in points

THEMES = {
    "light": {"surface": "#fcfcfb", "ink": "#0b0b0b", "ink2": "#52514e", "muted": "#898781", "grid": "#e1e0d9",
              "axis": "#c3c2b7", "series": ["#2a78d6", "#eb6834", "#1baf7a"],
              "ordinal": ["#86b6ef", "#3987e5", "#1c5cab", "#0d366b"], "neg": "#e34948", "mid": "#f0efec",
              "pos": "#2a78d6", "context": "#898781"},
    "dark": {"surface": "#1a1a19", "ink": "#f0efec", "ink2": "#c3c2b7", "muted": "#898781", "grid": "#2c2c2a",
             "axis": "#383835", "series": ["#3987e5", "#d95926", "#199e70"],
             "ordinal": ["#184f95", "#2a78d6", "#6da7ec", "#b7d3f6"], "neg": "#e66767", "mid": "#383835",
             "pos": "#3987e5", "context": "#898781"},
}


def style(theme: dict) -> None:
    plt.rcParams.update({
        "font.family": "DejaVu Sans", "font.size": 9, "figure.facecolor": theme["surface"],
        "axes.facecolor": theme["surface"], "savefig.facecolor": theme["surface"], "axes.edgecolor": theme["axis"],
        "axes.linewidth": PX, "axes.labelcolor": theme["muted"], "axes.labelsize": 9, "xtick.color": theme["muted"],
        "ytick.color": theme["muted"], "xtick.labelsize": 8.5, "ytick.labelsize": 8.5, "xtick.major.width": PX,
        "ytick.major.width": PX, "xtick.major.size": 3, "ytick.major.size": 0, "text.color": theme["ink"],
        "legend.frameon": False, "legend.fontsize": 8.5, "legend.labelcolor": theme["ink2"],
    })


def frame(ax, theme: dict, grid_axis: str = "y") -> None:
    for side in ("top", "right"):
        ax.spines[side].set_visible(False)
    ax.spines["left"].set_visible(False)
    ax.grid(axis=grid_axis, color=theme["grid"], linewidth=PX, linestyle="-")
    ax.set_axisbelow(True)


def titles(fig, theme: dict, title: str, subtitle: str, x: float = 0.06) -> None:
    fig.text(x, 0.965, title, fontsize=12.5, fontweight="bold", color=theme["ink"], va="top")
    fig.text(x, 0.905, subtitle, fontsize=9, color=theme["ink2"], va="top", linespacing=1.4)


def thousands(ax) -> None:
    ax.yaxis.set_major_formatter(matplotlib.ticker.FuncFormatter(lambda v, _: f"{v / 1000:+,.0f}K" if v else "0"))


def load_curve(name: str) -> pd.Series:
    c = pd.read_csv(RESULTS / "curves" / f"{name}.csv", parse_dates=["timestamp"])
    return c.set_index("timestamp")["equity"] - 100_000.0


def end_labels(ax, theme: dict, series: list[tuple[pd.Series, str]], min_gap_frac: float = 0.055) -> None:
    """Final value beside each line end; labels are spread apart with thin leader lines if they would collide."""
    lo, hi = ax.get_ylim()
    gap = (hi - lo) * min_gap_frac
    ends = sorted(((s.iloc[-1], s.index[-1], colour) for s, colour in series), key=lambda e: e[0])
    placed = []
    for value, x, colour in ends:
        y = value if not placed else max(value, placed[-1][0] + gap)
        placed.append((y, value, x, colour))
    x_text = ax.get_xlim()[1]
    for y, value, x, colour in placed:
        ax.plot([mdates.date2num(x)], [value], "o", ms=8 * PX, color=colour, mec=theme["surface"], mew=2 * PX, zorder=5,
                clip_on=False)
        if abs(y - value) > 1e-9:
            ax.plot([mdates.date2num(x), x_text], [value, y], color=theme["axis"], lw=PX, clip_on=False, zorder=4)
        ax.annotate(f"{value / 1000:+,.1f}K", (x_text, y), xytext=(6, 0), textcoords="offset points", va="center",
                    fontsize=8.5, color=theme["ink"], annotation_clip=False)


def cost_figure(report: dict, theme: dict, mode: str) -> None:
    rows = [r for r in report["A_costs"] if r["label"] == "MA(20,50)"]
    hold = next(r for r in report["A_costs"] if r["label"] == "Buy and hold" and r["cost"] == report["setup"]["baseline_costs"])
    fig, ax = plt.subplots(figsize=(8.6, 4.9), dpi=200)
    fig.subplots_adjust(left=0.08, right=0.87, top=0.69, bottom=0.09)
    labelled = []
    for row, colour in zip(rows, theme["ordinal"]):
        s = load_curve(row["curve"])
        ax.plot(s.index, s.values, color=colour, lw=2 * PX, solid_joinstyle="round", solid_capstyle="round",
                label=f"MA(20,50), {row['cost'].lower()}" + (" (stopped out)" if row["stopped_out"] else ""))
        labelled.append((s, colour))
    s = load_curve(hold["curve"])
    ax.plot(s.index, s.values, color=theme["context"], lw=1.5 * PX, label="Buy and hold, for reference")
    labelled.append((s, theme["context"]))
    ax.axhline(0, color=theme["axis"], lw=PX)
    frame(ax, theme)
    thousands(ax)
    ax.set_ylabel("Cumulative P&L, $")
    ax.margins(x=0)
    fig.legend(*ax.get_legend_handles_labels(), loc="upper left", bbox_to_anchor=(0.075, 0.815), ncol=3,
               handlelength=1.6, columnspacing=1.6)
    end_labels(ax, theme, labelled)
    gross = rows[0]["net_pnl"]
    base = next(r for r in rows if r["cost"] == report["setup"]["baseline_costs"])
    titles(fig, theme, "The crossover loses before costs, and costs widen the loss",
           f"MA(20,50) on EURUSD hourly bid quotes, development period 2004-2014. 100,000 units on $100,000.\n"
           f"Gross P&L {gross / 1000:+,.1f}K; at 0.5 pip + $3.50/side commission {base['net_pnl'] / 1000:+,.1f}K "
           f"({base['round_trips_per_year']:.0f} round trips a year).", x=0.08)
    fig.savefig(FIGURES / f"cost_sensitivity_{mode}.png")
    plt.close(fig)


def luminance(hex_colour: str) -> float:
    def channel(c):
        return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4
    r, g, b = (channel(c) for c in to_rgb(hex_colour))
    return 0.2126 * r + 0.7152 * g + 0.0722 * b


def grid_figure(report: dict, theme: dict, mode: str) -> None:
    grid = pd.DataFrame(report["B_grid"])
    test = pd.DataFrame(report["C_test_grid_after_selection"]).assign(period="test")
    data = pd.concat([grid, test])
    shorts, longs = sorted(data["short"].unique()), sorted(data["long"].unique())
    panels = [("development", "Development 2004-2014"), ("validation", "Validation 2015-2019 (selection)"),
              ("test", "Test 2020-2024 (after selection)")]
    sel = report["selection"]["selected"]
    m = float(np.ceil(np.nanmax(np.abs(data["sharpe_pnl_zero_rf"])) * 10) / 10)
    cmap = LinearSegmentedColormap.from_list("div", [theme["neg"], theme["mid"], theme["pos"]])
    norm = TwoSlopeNorm(vmin=-m, vcenter=0.0, vmax=m)

    fig, axes = plt.subplots(1, 3, figsize=(11.6, 4.5), dpi=200)
    fig.subplots_adjust(left=0.06, right=0.9, top=0.72, bottom=0.12, wspace=0.12)
    for ax, (period, name) in zip(axes, panels):
        cell = data[data.period == period].set_index(["short", "long"])["sharpe_pnl_zero_rf"]
        ax_w = ax.get_position().width * fig.get_figwidth() * 96
        gap = 2 / (ax_w / len(longs))  # 2px surface gap, in cell units
        for i, s in enumerate(shorts):
            for j, l in enumerate(longs):
                x, y = j + gap / 2, i + gap / 2
                if (s, l) not in cell.index:
                    ax.text(j + 0.5, i + 0.5, "n/a", ha="center", va="center", fontsize=8, color=theme["muted"])
                    continue
                v = float(cell[(s, l)])
                colour = matplotlib.colors.to_hex(cmap(norm(v)))
                ax.add_patch(Rectangle((x, y), 1 - gap, 1 - gap, color=colour, lw=0))
                ink = "#ffffff" if luminance(colour) < 0.3 else "#0b0b0b"
                ax.text(j + 0.5, i + 0.5, f"{v:+.2f}", ha="center", va="center", fontsize=8, color=ink)
                if s == sel["short"] and l == sel["long"]:
                    ax.add_patch(Rectangle((j + 0.03, i + 0.03), 0.94, 0.94, fill=False, ec=theme["ink"], lw=2 * PX))
        ax.set_xlim(0, len(longs))
        ax.set_ylim(0, len(shorts))
        ax.set_xticks(np.arange(len(longs)) + 0.5, [str(v) for v in longs])
        ax.set_yticks(np.arange(len(shorts)) + 0.5, [str(v) for v in shorts] if ax is axes[0] else [])
        ax.tick_params(length=0)
        for side in ax.spines.values():
            side.set_visible(False)
        ax.set_xlabel("Long window (hours)")
        ax.set_title(name, fontsize=9.5, color=theme["ink"], loc="left", pad=6)
    axes[0].set_ylabel("Short window (hours)")
    cax = fig.add_axes([0.915, 0.12, 0.012, 0.6])
    cb = fig.colorbar(matplotlib.cm.ScalarMappable(norm=norm, cmap=cmap), cax=cax)
    cb.outline.set_visible(False)
    cb.ax.tick_params(length=0, labelsize=8)
    cb.set_label("Sharpe ratio, annualised", color=theme["muted"], fontsize=8.5)
    sel_row = report["selection"]
    titles(fig, theme, "The best validation pair did not carry over",
           f"Sharpe of hourly P&L for each moving-average pair at 0.5 pip + commission. Outlined: MA({sel['short']},{sel['long']}), "
           f"chosen for the top validation Sharpe ({sel_row['validation_sharpe']:+.2f}).\n"
           f"Rank correlation between development and validation: {sel_row['validation_sharpe_rank_correlation_with_development']:+.2f}. "
           f"Best-of-24 Sharpe expected from luck alone: about {sel_row['luck_yardstick']['expected_best_of_n_sharpe_if_no_edge_and_independent']:.2f}.",
           x=0.06)
    fig.savefig(FIGURES / f"parameter_grid_{mode}.png")
    plt.close(fig)


def test_figure(report: dict, theme: dict, mode: str) -> None:
    rows = report["C_test"]
    curves = ["C_selected", "C_ma20_50", "C_buyhold"]
    fig, ax = plt.subplots(figsize=(8.6, 4.6), dpi=200)
    fig.subplots_adjust(left=0.08, right=0.87, top=0.72, bottom=0.09)
    labelled = []
    for row, name, colour in zip(rows, curves, theme["series"]):
        s = load_curve(name)
        ax.plot(s.index, s.values, color=colour, lw=2 * PX, solid_joinstyle="round", label=f"{row['label']}, {row['role']}")
        labelled.append((s, colour))
    ax.axhline(0, color=theme["axis"], lw=PX)
    frame(ax, theme)
    thousands(ax)
    ax.set_ylabel("Cumulative P&L, $")
    ax.margins(x=0)
    fig.legend(*ax.get_legend_handles_labels(), loc="upper left", bbox_to_anchor=(0.075, 0.82), ncol=3,
               handlelength=1.6, columnspacing=1.6)
    end_labels(ax, theme, labelled)
    sel = rows[0]
    titles(fig, theme, "One run on the held-out test period",
           f"EURUSD hourly, 2020-2024, 0.5 pip + $3.50/side commission. The selected pair lost money "
           f"(Sharpe {sel['sharpe_pnl_zero_rf']:+.2f});\nthe untuned MA(20,50) made money (Sharpe "
           f"{rows[1]['sharpe_pnl_zero_rf']:+.2f}), which is what noise looks like.", x=0.08)
    fig.savefig(FIGURES / f"test_period_{mode}.png")
    plt.close(fig)


def rounded_hbar(ax, y: float, width: float, height: float, colour: str, radius_px: float, px_x: float, px_y: float):
    """One path: square at the baseline, corners of `radius_px` at the data end (no seam between patches)."""
    rx, ry = min(radius_px * px_x, width / 2), min(radius_px * px_y, height / 2)
    k = 0.5522847498  # cubic Bezier approximation of a quarter circle
    x1, y0, y1 = width, y - height / 2, y + height / 2
    verts = [(0, y0), (x1 - rx, y0), (x1 - rx + k * rx, y0), (x1, y0 + ry - k * ry), (x1, y0 + ry), (x1, y1 - ry),
             (x1, y1 - ry + k * ry), (x1 - rx + k * rx, y1), (x1 - rx, y1), (0, y1), (0, y0)]
    codes = [Path.MOVETO, Path.LINETO, Path.CURVE4, Path.CURVE4, Path.CURVE4, Path.LINETO, Path.CURVE4, Path.CURVE4,
             Path.CURVE4, Path.LINETO, Path.CLOSEPOLY]
    ax.add_patch(PathPatch(Path(verts, codes), facecolor=colour, lw=0))


def runtime_figure(report: dict, theme: dict, mode: str) -> None:
    d = report["D_runtime"]
    runs = pd.DataFrame(d["runs"])
    full = runs[runs.bars == runs.bars.max()].set_index("implementation")
    order = ["Python loop", "NumPy (vectorised)", "C++ engine"]
    cpp = float(full.loc["C++ engine", "seconds"])
    labels = {impl: f"{full.loc[impl, 'seconds']:.2f} s" + ("" if impl == "C++ engine" else
              f"  ·  {full.loc[impl, 'seconds'] / cpp:.{0 if full.loc[impl, 'seconds'] / cpp >= 10 else 1}f}x slower")
              for impl in order}
    fig, ax = plt.subplots(figsize=(8.6, 3.2), dpi=200)
    fig.subplots_adjust(left=0.2, right=0.96, top=0.66, bottom=0.17)
    # Measure the labels first so the axis is wide enough for the longest bar plus its label.
    renderer = fig.canvas.get_renderer()
    axes_px = ax.get_window_extent(renderer).width
    xmax = 0.0
    for impl in order:
        probe = ax.text(0, 0, "  " + labels[impl], fontsize=8.5)
        text_px = probe.get_window_extent(renderer).width * 1.05
        probe.remove()
        xmax = max(xmax, float(full.loc[impl, "seconds"]) / (1 - text_px / axes_px))
    ax.set_xlim(0, xmax)
    ax.set_ylim(-0.6, len(order) - 0.4)
    bbox = ax.get_window_extent(renderer)
    px_x, px_y = xmax / bbox.width * (fig.dpi / 96), len(order) / bbox.height * (fig.dpi / 96)
    height = min(24 * px_y, 0.6)
    for i, impl in enumerate(order):
        sec = float(full.loc[impl, "seconds"])
        colour = theme["series"][0] if impl == "C++ engine" else theme["context"]
        rounded_hbar(ax, i, sec, height, colour, 4, px_x, px_y)
        ax.text(sec, i, "  " + labels[impl], va="center", fontsize=8.5, color=theme["ink"])
    ax.set_yticks(range(len(order)), order, color=theme["ink2"], fontsize=9)
    ax.tick_params(axis="y", length=0)
    frame(ax, theme, grid_axis="x")
    ax.spines["bottom"].set_visible(False)
    ax.set_xlabel("Seconds for one full backtest (lower is faster)")
    bars = int(full["bars"].iloc[0])
    titles(fig, theme, "Same backtest, three implementations",
           f"{bars / 1e6:.1f}M EURUSD 1-minute bars, MA(20,50) with costs; identical fills and final equity in all three.\n"
           f"Best of 5 runs (Python loop: 1 run) on a {d['machine']['logical_cpus']}-vCPU {d['machine']['cpu']}, "
           f"g++ {d['compiler']} Release, Python {d['machine']['python']}.", x=0.02)
    fig.savefig(FIGURES / f"runtime_{mode}.png")
    plt.close(fig)


def main() -> None:
    report = json.loads((RESULTS / "experiments.json").read_text())
    FIGURES.mkdir(parents=True, exist_ok=True)
    for mode, theme in THEMES.items():
        style(theme)
        cost_figure(report, theme, mode)
        grid_figure(report, theme, mode)
        test_figure(report, theme, mode)
    print("wrote figures to", FIGURES)


if __name__ == "__main__":
    main()
