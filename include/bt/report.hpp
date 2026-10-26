#pragma once
// Output files: equity curve and fills as CSV, metrics as JSON. The Python scripts read these for charts.

#include <map>
#include <string>

#include "bt/engine.hpp"
#include "bt/metrics.hpp"

namespace bt {

void write_equity_csv(const std::string& path, const BacktestResult& result);
void write_fills_csv(const std::string& path, const BacktestResult& result);
// `extra` holds run settings (strategy, windows, costs, data file) written alongside the metrics.
std::string metrics_json(const Metrics& m, const std::map<std::string, std::string>& extra);
void write_text(const std::string& path, const std::string& text);

}  // namespace bt
