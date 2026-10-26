#include "bt/csv.hpp"

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <system_error>

namespace bt {
namespace {

// Howard Hinnant's days_from_civil / civil_from_days (proleptic Gregorian calendar).
std::int64_t days_from_civil(std::int64_t y, unsigned m, unsigned d) {
    y -= m <= 2 ? 1 : 0;
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const auto yoe = static_cast<unsigned>(y - era * 400);
    const unsigned mp = m > 2 ? m - 3 : m + 9;
    const unsigned doy = (153 * mp + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

void civil_from_days(std::int64_t z, std::int64_t& y, unsigned& m, unsigned& d) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const auto doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    d = doy - (153 * mp + 2) / 5 + 1;
    m = mp < 10 ? mp + 3 : mp - 9;
    y = static_cast<std::int64_t>(yoe) + era * 400 + (m <= 2 ? 1 : 0);
}

bool read_int(std::string_view s, std::size_t pos, std::size_t len, int& out) {
    if (pos + len > s.size()) return false;
    int v = 0;
    for (std::size_t i = pos; i < pos + len; ++i) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return false;
        v = v * 10 + (s[i] - '0');
    }
    out = v;
    return true;
}

bool is_leap(int y) { return (y % 4 == 0 && y % 100 != 0) || y % 400 == 0; }

std::string_view trim(std::string_view s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::string lower(std::string_view s) {
    std::string out(s);
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return out;
}

// Splits on commas without copying: the views point into `line`.
void split_csv_line(std::string_view line, std::vector<std::string_view>& fields) {
    fields.clear();
    std::size_t start = 0;
    while (true) {
        const std::size_t comma = line.find(',', start);
        if (comma == std::string_view::npos) {
            fields.push_back(line.substr(start));
            return;
        }
        fields.push_back(line.substr(start, comma - start));
        start = comma + 1;
    }
}

// Whole-field number parse. std::from_chars is locale-independent and allocation-free; compilers without the
// floating-point overload fall back to strtod on a copy.
bool parse_double(std::string_view t, double& out) {
    if (t.size() > 1 && t.front() == '+') t.remove_prefix(1);
    if (t.empty()) return false;
#if defined(__cpp_lib_to_chars) && __cpp_lib_to_chars >= 201611L
    const auto [ptr, ec] = std::from_chars(t.data(), t.data() + t.size(), out);
    return ec == std::errc() && ptr == t.data() + t.size();
#else
    const std::string copy(t);
    errno = 0;
    char* end = nullptr;
    out = std::strtod(copy.c_str(), &end);
    return end != copy.c_str() && *end == '\0' && errno != ERANGE;
#endif
}

// Throws a message without location; the CSV reader adds file and line.
double parse_price(std::string_view text) {
    const std::string_view t = trim(text);
    if (t.empty()) throw std::runtime_error("missing price value");
    double v = 0.0;
    if (!parse_double(t, v)) throw std::runtime_error("cannot parse price '" + std::string(t) + "'");
    return v;
}

std::int64_t parse_timestamp_view(std::string_view raw) {
    const std::string_view s = trim(raw);
    int y = 0, mo = 0, d = 0, h = 0, mi = 0, se = 0;
    const bool date_ok = s.size() >= 10 && read_int(s, 0, 4, y) && s[4] == '-' && read_int(s, 5, 2, mo) &&
                         s[7] == '-' && read_int(s, 8, 2, d);
    if (!date_ok) throw std::runtime_error("bad timestamp '" + std::string(s) + "' (expected YYYY-MM-DD[ HH:MM[:SS]])");
    if (s.size() > 10) {
        const bool time_ok = (s[10] == ' ' || s[10] == 'T') && read_int(s, 11, 2, h) && s.size() >= 16 &&
                             s[13] == ':' && read_int(s, 14, 2, mi) &&
                             (s.size() == 16 || (s.size() == 19 && s[16] == ':' && read_int(s, 17, 2, se)));
        if (!time_ok) throw std::runtime_error("bad timestamp '" + std::string(s) + "' (expected YYYY-MM-DD[ HH:MM[:SS]])");
    }
    static const int month_days[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (mo < 1 || mo > 12) throw std::runtime_error("bad month in timestamp '" + std::string(s) + "'");
    const int dim = month_days[mo - 1] + (mo == 2 && is_leap(y) ? 1 : 0);
    if (d < 1 || d > dim || h > 23 || mi > 59 || se > 59)
        throw std::runtime_error("out-of-range field in timestamp '" + std::string(s) + "'");
    return days_from_civil(y, static_cast<unsigned>(mo), static_cast<unsigned>(d)) * 86400 + h * 3600 + mi * 60 + se;
}

// Parses one line at a time, so files are streamed in fixed-size chunks and never held in memory whole.
class BarParser {
public:
    explicit BarParser(std::string source) : source_(std::move(source)) {}

    void line(std::string_view line) {
        ++line_no_;
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (!have_header_) {
            header(line);
            return;
        }
        if (trim(line).empty()) return;
        split_csv_line(line, fields_);
        if (fields_.size() < needed_) throw error("expected at least " + std::to_string(needed_) + " fields");
        const auto at = [&](int k) { return fields_[static_cast<std::size_t>(col_[k])]; };
        Bar b;
        try {
            b.timestamp = parse_timestamp_view(at(0));
            b.open = parse_price(at(1));
            b.high = parse_price(at(2));
            b.low = parse_price(at(3));
            b.close = parse_price(at(4));
        } catch (const std::runtime_error& e) {
            throw error(e.what());
        }
        const std::string problem = validate_bar(b);
        if (!problem.empty()) throw error(problem);
        if (!bars_.empty()) {
            if (b.timestamp == bars_.back().timestamp) throw error("duplicate timestamp " + format_timestamp(b.timestamp));
            if (b.timestamp < bars_.back().timestamp) throw error("timestamps are not in chronological order");
        }
        bars_.push_back(b);
    }

    std::size_t lines() const { return line_no_; }
    void reserve(std::size_t n) { bars_.reserve(n); }

    std::vector<Bar> finish() {
        if (!have_header_) throw std::runtime_error(source_ + ": file is empty");
        if (bars_.empty()) throw std::runtime_error(source_ + ": no data rows");
        return std::move(bars_);
    }

private:
    void header(std::string_view line) {
        if (line.size() >= 3 && static_cast<unsigned char>(line[0]) == 0xEF && static_cast<unsigned char>(line[1]) == 0xBB &&
            static_cast<unsigned char>(line[2]) == 0xBF)
            line.remove_prefix(3);  // UTF-8 byte-order mark
        split_csv_line(line, fields_);
        const char* required[5] = {"timestamp", "open", "high", "low", "close"};
        for (std::size_t i = 0; i < fields_.size(); ++i) {
            const std::string name = lower(trim(fields_[i]));
            for (int k = 0; k < 5; ++k)
                if (name == required[k] || (k == 0 && (name == "time" || name == "datetime"))) col_[k] = static_cast<int>(i);
        }
        std::string missing;
        for (int k = 0; k < 5; ++k)
            if (col_[k] < 0) missing += (missing.empty() ? "" : ", ") + std::string(required[k]);
        if (!missing.empty()) throw std::runtime_error(source_ + ": missing column(s): " + missing);
        needed_ = static_cast<std::size_t>(*std::max_element(col_, col_ + 5)) + 1;
        have_header_ = true;
    }

    std::runtime_error error(const std::string& message) const {
        return std::runtime_error(source_ + " line " + std::to_string(line_no_) + ": " + message);
    }

    std::string source_;
    std::size_t line_no_ = 0;
    bool have_header_ = false;
    int col_[5] = {-1, -1, -1, -1, -1};
    std::size_t needed_ = 0;
    std::vector<std::string_view> fields_;
    std::vector<Bar> bars_;
};

// Reads 1 MiB at a time and hands complete lines to the parser; a line split across chunks is joined first.
// `size_hint` (bytes, 0 if unknown) lets the bar vector be reserved once from the first chunk's line length.
std::vector<Bar> parse_stream(std::istream& in, const std::string& source_name, std::size_t size_hint) {
    BarParser parser(source_name);
    std::vector<char> chunk(std::size_t{1} << 20);
    std::string carry;
    bool reserved = size_hint == 0;
    std::size_t consumed = 0;
    while (in) {
        in.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        const auto got = static_cast<std::size_t>(in.gcount());
        if (got == 0) break;
        const std::string_view data(chunk.data(), got);
        std::size_t start = 0;
        for (std::size_t nl = data.find('\n'); nl != std::string_view::npos; nl = data.find('\n', start)) {
            if (carry.empty()) {
                parser.line(data.substr(start, nl - start));
            } else {
                carry.append(data.substr(start, nl - start));
                parser.line(carry);
                carry.clear();
            }
            start = nl + 1;
        }
        carry.append(data.substr(start));
        consumed += got;
        if (!reserved && parser.lines() > 1) {
            const double bytes_per_line = static_cast<double>(consumed - carry.size()) / static_cast<double>(parser.lines());
            parser.reserve(static_cast<std::size_t>(static_cast<double>(size_hint) / bytes_per_line * 1.02) + 16);
            reserved = true;
        }
    }
    if (!carry.empty()) parser.line(carry);
    return parser.finish();
}

}  // namespace

std::int64_t parse_timestamp(const std::string& raw) { return parse_timestamp_view(raw); }

std::string format_timestamp(std::int64_t seconds) {
    std::int64_t days = seconds / 86400;
    std::int64_t rem = seconds % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    std::int64_t y = 0;
    unsigned m = 0, d = 0;
    civil_from_days(days, y, m, d);
    char buf[80];  // sized for any int64 year, so the output can never be truncated
    std::snprintf(buf, sizeof buf, "%04lld-%02u-%02u %02lld:%02lld:%02lld", static_cast<long long>(y), m, d,
                  static_cast<long long>(rem / 3600), static_cast<long long>((rem % 3600) / 60),
                  static_cast<long long>(rem % 60));
    return buf;
}

std::string validate_bar(const Bar& b) {
    for (double v : {b.open, b.high, b.low, b.close}) {
        if (!std::isfinite(v)) return "non-finite price";
        if (v <= 0.0) return "prices must be positive";
    }
    if (b.high < std::max({b.open, b.close, b.low})) return "high is below open, close or low";
    if (b.low > std::min({b.open, b.close, b.high})) return "low is above open, close or high";
    return {};
}

std::vector<Bar> parse_bars_csv(std::istream& in, const std::string& source_name) {
    return parse_stream(in, source_name, 0);
}

std::vector<Bar> load_bars_csv(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    f.seekg(0, std::ios::end);
    const std::streamoff size = f.tellg();
    f.seekg(0, std::ios::beg);
    return parse_stream(f, path, size > 0 ? static_cast<std::size_t>(size) : 0);
}

std::vector<TimedTarget> parse_targets_csv(std::istream& in, const std::string& source_name) {
    std::string line;
    std::size_t line_no = 0;
    auto error = [&](const std::string& message) {
        return std::runtime_error(source_name + " line " + std::to_string(line_no) + ": " + message);
    };
    auto strip = [](std::string_view s) {
        if (!s.empty() && s.back() == '\r') s.remove_suffix(1);
        return s;
    };

    if (!std::getline(in, line)) throw std::runtime_error(source_name + ": file is empty");
    ++line_no;
    std::string_view header = strip(line);
    if (header.size() >= 3 && static_cast<unsigned char>(header[0]) == 0xEF && static_cast<unsigned char>(header[1]) == 0xBB &&
        static_cast<unsigned char>(header[2]) == 0xBF)
        header.remove_prefix(3);
    std::vector<std::string_view> fields;
    split_csv_line(header, fields);
    int ts_col = -1, target_col = -1;
    for (std::size_t i = 0; i < fields.size(); ++i) {
        const std::string name = lower(trim(fields[i]));
        if (name == "timestamp" || name == "time" || name == "datetime") ts_col = static_cast<int>(i);
        else if (name == "target") target_col = static_cast<int>(i);
    }
    std::string missing;
    if (ts_col < 0) missing = "timestamp";
    if (target_col < 0) missing += (missing.empty() ? "" : ", ") + std::string("target");
    if (!missing.empty()) throw std::runtime_error(source_name + ": missing column(s): " + missing);
    const auto needed = static_cast<std::size_t>(std::max(ts_col, target_col)) + 1;

    std::vector<TimedTarget> out;
    while (std::getline(in, line)) {
        ++line_no;
        const std::string_view row = strip(line);
        if (trim(row).empty()) continue;
        split_csv_line(row, fields);
        if (fields.size() < needed) throw error("expected at least " + std::to_string(needed) + " fields");
        TimedTarget t;
        try {
            t.timestamp = parse_timestamp_view(fields[static_cast<std::size_t>(ts_col)]);
        } catch (const std::runtime_error& e) {
            throw error(e.what());
        }
        const std::string_view value = trim(fields[static_cast<std::size_t>(target_col)]);
        if (!parse_double(value, t.target)) throw error("cannot parse target '" + std::string(value) + "'");
        if (!std::isfinite(t.target)) throw error("non-finite target");
        if (!out.empty()) {
            if (t.timestamp == out.back().timestamp) throw error("duplicate timestamp " + format_timestamp(t.timestamp));
            if (t.timestamp < out.back().timestamp) throw error("timestamps are not in chronological order");
        }
        out.push_back(t);
    }
    if (out.empty()) throw std::runtime_error(source_name + ": no target rows");
    return out;
}

std::vector<TimedTarget> load_targets_csv(const std::string& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + path);
    return parse_targets_csv(f, path);
}

}  // namespace bt
