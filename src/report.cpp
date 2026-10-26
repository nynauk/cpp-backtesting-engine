#include "bt/report.hpp"

#include <cmath>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <stdexcept>

#include "bt/csv.hpp"

namespace bt {
namespace {

std::string number(double v) {
    if (!std::isfinite(v)) return "null";
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.10g", v);
    return buf;
}

std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (char c : s) {
        if (c == '"' || c == '\\') out += '\\';
        out += c;
    }
    return out + "\"";
}

}  // namespace

void write_text(const std::string& path, const std::string& text) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << text;
}

void write_equity_csv(const std::string& path, const BacktestResult& r) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << "timestamp,close,position,cash,equity\n";
    char buf[200];
    // 12 significant digits: exact for 5-decimal FX quotes and sub-cent for any realistic account size.
    for (const auto& p : r.equity) {
        std::snprintf(buf, sizeof buf, "%s,%.12g,%.12g,%.12g,%.12g\n", format_timestamp(p.timestamp).c_str(), p.close,
                      p.position, p.cash, p.equity);
        f << buf;
    }
}

void write_fills_csv(const std::string& path, const BacktestResult& r) {
    std::ofstream f(path);
    if (!f) throw std::runtime_error("cannot write " + path);
    f << "timestamp,side,quantity,price,fee\n";
    char buf[200];
    for (const auto& x : r.fills) {
        std::snprintf(buf, sizeof buf, "%s,%s,%.12g,%.12g,%.12g\n", format_timestamp(x.timestamp).c_str(),
                      x.side == Side::Buy ? "BUY" : "SELL", x.quantity, x.price, x.fee);
        f << buf;
    }
}

std::string metrics_json(const Metrics& m, const std::map<std::string, std::string>& extra) {
    std::ostringstream o;
    o << "{\n";
    for (const auto& kv : extra) o << "  " << quoted(kv.first) << ": " << quoted(kv.second) << ",\n";
    o << "  \"observations\": " << m.observations << ",\n"
      << "  \"years\": " << number(m.years) << ",\n"
      << "  \"periods_per_year\": " << number(m.periods_per_year) << ",\n"
      << "  \"final_equity\": " << number(m.final_equity) << ",\n"
      << "  \"ruined\": " << (m.ruined ? "true" : "false") << ",\n"
      << "  \"stopped_out\": " << (m.stopped_out ? "true" : "false") << ",\n"
      << "  \"stop_out_at\": " << (m.stopped_out ? quoted(format_timestamp(m.stop_out_timestamp)) : "null") << ",\n"
      << "  \"total_return\": " << number(m.total_return) << ",\n"
      << "  \"cagr\": " << number(m.cagr) << ",\n"
      << "  \"annual_volatility\": " << number(m.annual_volatility) << ",\n"
      << "  \"sharpe_zero_rf\": " << number(m.sharpe) << ",\n"
      << "  \"max_drawdown\": " << number(m.max_drawdown) << ",\n"
      << "  \"net_pnl\": " << number(m.net_pnl) << ",\n"
      << "  \"annual_return_on_capital\": " << number(m.annual_return_on_capital) << ",\n"
      << "  \"annual_volatility_on_capital\": " << number(m.annual_volatility_on_capital) << ",\n"
      << "  \"sharpe_pnl_zero_rf\": " << number(m.sharpe_pnl) << ",\n"
      << "  \"max_drawdown_on_capital\": " << number(m.max_drawdown_on_capital) << ",\n"
      << "  \"fills\": " << m.fills << ",\n"
      << "  \"exposure\": " << number(m.exposure) << ",\n"
      << "  \"turnover_per_year\": " << number(m.turnover) << ",\n"
      << "  \"fees_paid\": " << number(m.fees_paid) << ",\n"
      << "  \"spread_slippage_cost\": " << number(m.spread_slippage_cost) << "\n"
      << "}\n";
    return o.str();
}

}  // namespace bt
