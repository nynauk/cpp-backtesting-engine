"""Plant known bugs in a copy of the engine, one at a time, and check that the test suite catches each one.

  python tools/mutation_check.py

Every mutant is a small, realistic mistake (look-ahead, a cost sign, a missed fee, an off-by-one warm-up...).
A mutant is "caught" when at least one GoogleTest case fails against it. The source tree is never touched: the
mutants are applied to a copy under build/mutation/. Writes build/mutation/results.json.
"""
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / "build" / "mutation" / "src"
BUILD = ROOT / "build" / "mutation" / "build"
GTEST = ROOT / "build" / "_deps" / "googletest-src"  # reused if the main build already downloaded it

MUTANTS = [
    ("look-ahead: decide and fill on the same bar's close", "src/engine.cpp", [
        ("        // 3. The strategy sees bars[0..t] only and sets the next target.\n"
         "        if (!halted && !liquidating) {\n"
         "            pending_target = strategy.target_position(History(bars.data(), t + 1));\n"
         "            have_pending = true;\n        }\n", ""),
        ("        const bool in_window = bar.timestamp >= cfg.start;\n",
         "        const bool in_window = bar.timestamp >= cfg.start;\n"
         "        if (!halted && !liquidating) {\n"
         "            pending_target = strategy.target_position(History(bars.data(), t + 1));\n"
         "            have_pending = true;\n        }\n"),
        ("execution_price(cfg.costs, f.side, bar.open)", "execution_price(cfg.costs, f.side, bar.close)"),
    ]),
    ("fill at the next bar's close instead of its open", "src/engine.cpp", [
        ("execution_price(cfg.costs, f.side, bar.open)", "execution_price(cfg.costs, f.side, bar.close)")]),
    ("trade the full target every bar instead of the change", "src/engine.cpp", [
        ("const double delta = target - portfolio.position();", "const double delta = target;")]),
    ("keep running past the end of the evaluation window", "src/engine.cpp", [
        ("if (bar.timestamp >= cfg.end) break;", "if (false) break;")]),
    ("stop-out never triggers", "src/engine.cpp", [
        ("if (!halted && !liquidating && portfolio.equity() <= cfg.stop_out_equity) {", "if (false) {")]),
    ("stop-out leaves the position open", "src/engine.cpp", [
        ("const double target = liquidating ? 0.0 : pending_target;", "const double target = pending_target;")]),
    ("trading resumes after a stop-out", "src/engine.cpp", [
        ("                liquidating = false;\n                halted = true;\n", "                liquidating = false;\n")]),
    ("mid basis: sells skip the half-spread", "src/portfolio.cpp", [
        ("quote - half - c.slippage", "quote - c.slippage")]),
    ("bid basis: sells pay the spread a second time", "src/portfolio.cpp", [
        ("quote + c.spread + c.slippage : quote - c.slippage;", "quote + c.spread + c.slippage : quote - c.spread - c.slippage;")]),
    ("fees never leave cash", "src/portfolio.cpp", [("    cash_ -= f.fee;\n", "")]),
    ("average price ignores trade sizes", "src/portfolio.cpp", [
        ("average_price_ = (std::abs(old) * average_price_ + f.quantity * f.price) / std::abs(now);",
         "average_price_ = old == 0.0 ? f.price : 0.5 * (average_price_ + f.price);")]),
    ("realised P&L ignores position direction", "src/portfolio.cpp", [
        ("realised_pnl_ += closed * (f.price - average_price_) * direction;",
         "realised_pnl_ += closed * (f.price - average_price_);")]),
    ("crossover signals one bar before the long window is full", "src/strategies.cpp", [
        ("if (t + 1 < long_window_) return 0.0;", "if (t + 2 < long_window_) return 0.0;")]),
    ("crossover waits one bar too long", "src/strategies.cpp", [
        ("if (t + 1 < long_window_) return 0.0;", "if (t < long_window_) return 0.0;")]),
    ("history view lets a strategy read one bar ahead", "include/bt/strategy.hpp", [
        ("if (i >= size_)", "if (i > size_)")]),
    ("duplicate timestamps accepted", "src/csv.cpp", [
        ("if (b.timestamp == bars_.back().timestamp)", "if (false)")]),
    ("a line split across read chunks is parsed as two lines", "src/csv.cpp", [
        ("                carry.append(data.substr(start, nl - start));\n                parser.line(carry);\n",
         "                parser.line(carry);\n                parser.line(data.substr(start, nl - start));\n")]),
    ("last line without a newline is dropped", "src/csv.cpp", [
        ("    if (!carry.empty()) parser.line(carry);\n", "")]),
    ("drawdown measured from starting cash, not the running peak", "src/metrics.cpp", [
        ("        peak = std::max(peak, e);\n", "")]),
    ("365-day years", "src/metrics.cpp", [("365.25 * 86400.0", "365.0 * 86400.0")]),
    ("capital drawdown measured from starting cash, not the running peak", "src/metrics.cpp", [
        ("(e - peak) / r.initial_cash", "(e - r.initial_cash) / r.initial_cash")]),
    ("ruined accounts still get compounding metrics", "src/metrics.cpp", [
        ("if (e <= 0.0) m.ruined = true;", "if (e < -1e300) m.ruined = true;")]),
]


def run(cmd, **kw):
    return subprocess.run(cmd, capture_output=True, text=True, **kw)


def test_binary() -> Path:
    suffix = ".exe" if os.name == "nt" else ""
    for candidate in (BUILD / f"bt_tests{suffix}", BUILD / "Release" / f"bt_tests{suffix}"):
        if candidate.exists():
            return candidate
    sys.exit("bt_tests was not built")


def build() -> bool:
    return run(["cmake", "--build", str(BUILD), "--target", "bt_tests", "--config", "Release"]).returncode == 0


def main():
    if WORK.exists():
        shutil.rmtree(WORK)
    shutil.copytree(ROOT, WORK, ignore=shutil.ignore_patterns("build", "build-*", ".git", "data", "results", "python", "__pycache__"))
    cmd = ["cmake", "-S", str(WORK), "-B", str(BUILD), "-DCMAKE_BUILD_TYPE=Release", "-DBT_BUILD_BENCHMARKS=OFF"]
    if shutil.which("ninja"):
        cmd += ["-G", "Ninja"]
    if GTEST.exists():
        cmd.append(f"-DFETCHCONTENT_SOURCE_DIR_GOOGLETEST={GTEST}")
    configured = run(cmd)
    if configured.returncode:
        sys.exit(configured.stdout + configured.stderr)
    if not build() or run([str(test_binary())]).returncode:
        sys.exit("the unmodified code does not build or pass its tests")

    results = []
    for name, rel, edits in MUTANTS:
        path = WORK / rel
        original = path.read_text()
        text = original
        for old, new in edits:
            if text.count(old) != 1:
                sys.exit(f"{name}: expected the pattern once in {rel}, found it {text.count(old)} times: {old!r}")
            text = text.replace(old, new)
        path.write_text(text)
        try:
            if not build():
                results.append({"mutant": name, "outcome": "did not compile"})
                continue
            t = run([str(test_binary()), "--gtest_brief=1"], timeout=300)
            failed = sorted(set(re.findall(r"\[  FAILED  \] (\w+\.\w+)", t.stdout)))
            crashed = t.returncode != 0 and not failed
            results.append({"mutant": name, "outcome": "caught" if (failed or crashed) else "SURVIVED",
                            "failing_tests": failed or (["(crashed)"] if crashed else [])})
        finally:
            path.write_text(original)

    caught = sum(r["outcome"] == "caught" for r in results)
    for r in results:
        print(f"{r['outcome']:>9}  {r['mutant']}")
    print(f"\n{caught}/{len(results)} planted bugs caught")
    (BUILD.parent / "results.json").write_text(json.dumps({"caught": caught, "total": len(results), "results": results}, indent=2))
    sys.exit(0 if caught == len(results) else 1)


if __name__ == "__main__":
    main()
