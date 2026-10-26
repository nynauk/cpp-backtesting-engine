#include <gtest/gtest.h>

#include <sstream>
#include <stdexcept>
#include <string>

#include "bt/csv.hpp"

namespace {

std::vector<bt::Bar> parse(const std::string& text) {
    std::istringstream in(text);
    return bt::parse_bars_csv(in, "test.csv");
}

std::string error_of(const std::string& text) {
    try {
        parse(text);
    } catch (const std::runtime_error& e) {
        return e.what();
    }
    return "";
}

}  // namespace

TEST(Timestamp, KnownValuesAndRoundTrip) {
    EXPECT_EQ(bt::parse_timestamp("1970-01-01 00:00:00"), 0);
    // 2004-01-01 is day 12418 after the epoch: 12418 * 86400 + 19 * 3600.
    EXPECT_EQ(bt::parse_timestamp("2004-01-01 19:00:00"), 12418LL * 86400 + 19 * 3600);
    EXPECT_EQ(bt::parse_timestamp("2004-01-01T19:00:00"), bt::parse_timestamp("2004-01-01 19:00"));
    EXPECT_EQ(bt::parse_timestamp("2024-03-01") - bt::parse_timestamp("2024-02-28"), 2 * 86400);  // leap year
    EXPECT_EQ(bt::parse_timestamp("2023-03-01") - bt::parse_timestamp("2023-02-28"), 1 * 86400);
    for (const std::string s : {"2024-02-29 23:59:59", "1999-12-31 00:00:01", "2000-01-01 12:30:00"})
        EXPECT_EQ(bt::format_timestamp(bt::parse_timestamp(s)), s);
}

TEST(Timestamp, RejectsMalformedOrImpossibleDates) {
    EXPECT_THROW(bt::parse_timestamp("2024-02-30"), std::runtime_error);
    EXPECT_THROW(bt::parse_timestamp("2023-02-29"), std::runtime_error);
    EXPECT_THROW(bt::parse_timestamp("2024-13-01"), std::runtime_error);
    EXPECT_THROW(bt::parse_timestamp("2024-01-01 24:00:00"), std::runtime_error);
    EXPECT_THROW(bt::parse_timestamp("01/02/2024"), std::runtime_error);
    EXPECT_THROW(bt::parse_timestamp("2024-01-01 9:00"), std::runtime_error);
}

TEST(Csv, ParsesAnyColumnOrderExtraColumnsAndCrlf) {
    const auto bars = parse("close,low,volume,high,open,timestamp\r\n"
                            "1.2,1.0,50,1.3,1.1,2024-01-02 00:00:00\r\n"
                            "1.25,1.1,60,1.3,1.2,2024-01-02 01:00:00\r\n");
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_DOUBLE_EQ(bars[0].open, 1.1);
    EXPECT_DOUBLE_EQ(bars[0].high, 1.3);
    EXPECT_DOUBLE_EQ(bars[0].low, 1.0);
    EXPECT_DOUBLE_EQ(bars[0].close, 1.2);
    EXPECT_EQ(bars[1].timestamp - bars[0].timestamp, 3600);
}

TEST(Csv, RejectsMissingColumns) {
    EXPECT_NE(error_of("timestamp,open,high,close\n2024-01-01,1,1,1\n").find("missing column(s): low"), std::string::npos);
}

TEST(Csv, RejectsDuplicateTimestamps) {
    const std::string e = error_of("timestamp,open,high,low,close\n"
                                   "2024-01-01 00:00,1,1,1,1\n"
                                   "2024-01-01 00:00,1,1,1,1\n");
    EXPECT_NE(e.find("line 3"), std::string::npos);
    EXPECT_NE(e.find("duplicate timestamp"), std::string::npos);
}

TEST(Csv, RejectsOutOfOrderTimestampsInsteadOfSorting) {
    EXPECT_NE(error_of("timestamp,open,high,low,close\n"
                       "2024-01-02,1,1,1,1\n"
                       "2024-01-01,1,1,1,1\n")
                  .find("chronological"),
              std::string::npos);
}

TEST(Csv, RejectsInvalidPrices) {
    const std::string head = "timestamp,open,high,low,close\n";
    EXPECT_NE(error_of(head + "2024-01-01,0,1,1,1\n").find("positive"), std::string::npos);
    EXPECT_NE(error_of(head + "2024-01-01,-1,1,1,1\n").find("positive"), std::string::npos);
    EXPECT_NE(error_of(head + "2024-01-01,1.1,1.05,1.0,1.08\n").find("high is below"), std::string::npos);
    EXPECT_NE(error_of(head + "2024-01-01,1.0,1.2,1.05,1.1\n").find("low is above"), std::string::npos);
    EXPECT_NE(error_of(head + "2024-01-01,1.0,abc,1.0,1.0\n").find("cannot parse"), std::string::npos);
    EXPECT_NE(error_of(head + "2024-01-01,1.0,,1.0,1.0\n").find("missing price"), std::string::npos);
    EXPECT_NE(error_of(head + "2024-01-01,1.0,nan,1.0,1.0\n").find("non-finite"), std::string::npos);
}

TEST(Csv, StreamsLinesThatStraddleReadChunks) {
    // About 1.7 MB of rows: the reader works in 1 MiB chunks, so some lines arrive in two pieces.
    std::ostringstream out;
    out << "timestamp,open,high,low,close\n";
    const std::int64_t t0 = bt::parse_timestamp("2020-01-01");
    const int n = 30000;
    for (int i = 0; i < n; ++i) {
        const double p = 1.0 + i * 1e-5;
        out << bt::format_timestamp(t0 + i * 60) << ',' << p << ',' << p + 0.001 << ',' << p - 0.001 << ',' << p << '\n';
    }
    ASSERT_GT(out.str().size(), std::size_t{1} << 20);
    const auto bars = parse(out.str());
    ASSERT_EQ(bars.size(), static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) {
        ASSERT_EQ(bars[static_cast<std::size_t>(i)].timestamp, t0 + i * 60);
        ASSERT_NEAR(bars[static_cast<std::size_t>(i)].open, 1.0 + i * 1e-5, 1e-9);
    }
}

TEST(Csv, HandlesByteOrderMarkBlankLinesAndNoFinalNewline) {
    const auto bars = parse("\xEF\xBB\xBFtimestamp,open,high,low,close\n2024-01-01,1,1,1,1\n\n   \n2024-01-02,1.5,1.5,1.5,1.5");
    ASSERT_EQ(bars.size(), 2u);
    EXPECT_DOUBLE_EQ(bars[1].close, 1.5);
    // Line numbers in errors still count the skipped blank lines.
    EXPECT_NE(error_of("timestamp,open,high,low,close\n\n2024-01-01,1,x,1,1\n").find("line 3"), std::string::npos);
}

TEST(TargetsCsv, ParsesAndValidatesTargetFiles) {
    std::istringstream good("model,target,timestamp\r\nlogit,1.5,2024-01-01 01:00:00\r\nlogit,-2,2024-01-01 02:00:00\r\n");
    const auto rows = bt::parse_targets_csv(good, "t.csv");
    ASSERT_EQ(rows.size(), 2u);
    EXPECT_DOUBLE_EQ(rows[0].target, 1.5);
    EXPECT_EQ(rows[1].timestamp - rows[0].timestamp, 3600);

    auto error = [](const std::string& text) {
        std::istringstream in(text);
        try {
            bt::parse_targets_csv(in, "t.csv");
        } catch (const std::runtime_error& e) {
            return std::string(e.what());
        }
        return std::string();
    };
    EXPECT_NE(error("timestamp,position\n2024-01-01,1\n").find("missing column(s): target"), std::string::npos);
    EXPECT_NE(error("timestamp,target\n2024-01-01,1\n2024-01-01,1\n").find("duplicate"), std::string::npos);
    EXPECT_NE(error("timestamp,target\n2024-01-02,1\n2024-01-01,1\n").find("chronological"), std::string::npos);
    EXPECT_NE(error("timestamp,target\n2024-01-01,inf\n").find("non-finite"), std::string::npos);
    EXPECT_NE(error("timestamp,target\n2024-01-01,up\n").find("cannot parse target"), std::string::npos);
    EXPECT_NE(error("timestamp,target\n").find("no target rows"), std::string::npos);
}

TEST(Csv, RejectsEmptyFiles) {
    EXPECT_NE(error_of("").find("empty"), std::string::npos);
    EXPECT_NE(error_of("timestamp,open,high,low,close\n").find("no data rows"), std::string::npos);
}
