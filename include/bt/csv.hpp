#pragma once
// Loading and validating OHLC data.
//
// Data contract: a header row containing `timestamp,open,high,low,close` (any order, extra columns ignored),
// then one bar per line. Timestamps are "YYYY-MM-DD HH:MM:SS", "YYYY-MM-DDTHH:MM:SS", "YYYY-MM-DD HH:MM" or
// "YYYY-MM-DD". The loader never sorts, fills or drops rows: anything that breaks the contract raises
// std::runtime_error naming the source and line, so bad data cannot slip silently into a backtest.
// Input is streamed in 1 MiB chunks and parsed without per-field allocations, so memory use is the bars alone.

#include <cstdint>
#include <istream>
#include <string>
#include <vector>

#include "bt/types.hpp"

namespace bt {

// Seconds since 1970-01-01 for a calendar timestamp, with no time-zone conversion. Throws on bad input.
std::int64_t parse_timestamp(const std::string& text);

// Inverse of parse_timestamp: "YYYY-MM-DD HH:MM:SS".
std::string format_timestamp(std::int64_t seconds);

// Checks one bar: all prices finite and positive, high >= max(open, close, low), low <= min(open, close, high).
// Returns an empty string when valid, otherwise a description of the problem.
std::string validate_bar(const Bar& bar);

std::vector<Bar> parse_bars_csv(std::istream& in, const std::string& source_name = "<stream>");
std::vector<Bar> load_bars_csv(const std::string& path);

// Target positions from another program: a header with `timestamp` and `target` (any order, extra columns
// ignored), timestamps strictly increasing, targets finite. Same timestamp formats and error style as bars.
std::vector<TimedTarget> parse_targets_csv(std::istream& in, const std::string& source_name = "<stream>");
std::vector<TimedTarget> load_targets_csv(const std::string& path);

}  // namespace bt
