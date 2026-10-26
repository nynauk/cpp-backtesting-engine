#include <gtest/gtest.h>

#include <random>
#include <stdexcept>

#include "bt/costs.hpp"
#include "bt/portfolio.hpp"

namespace {

bt::Fill fill(bt::Side side, double qty, double price, double fee = 0.0) {
    bt::Fill f;
    f.side = side;
    f.quantity = qty;
    f.price = price;
    f.fee = fee;
    return f;
}

constexpr double kTol = 1e-9;

}  // namespace

TEST(Portfolio, BuyReducesCashAndIncreasesPosition) {
    bt::Portfolio p(100000.0);
    p.apply_fill(fill(bt::Side::Buy, 1000, 1.25));
    EXPECT_NEAR(p.cash(), 100000.0 - 1250.0, kTol);
    EXPECT_DOUBLE_EQ(p.position(), 1000.0);
    EXPECT_DOUBLE_EQ(p.average_price(), 1.25);
}

TEST(Portfolio, SellReversesThePositionChangeAndUpdatesCash) {
    bt::Portfolio p(100000.0);
    p.apply_fill(fill(bt::Side::Buy, 1000, 1.25));
    p.apply_fill(fill(bt::Side::Sell, 1000, 1.26));
    EXPECT_NEAR(p.cash(), 100000.0 - 1250.0 + 1260.0, kTol);
    EXPECT_DOUBLE_EQ(p.position(), 0.0);
    EXPECT_NEAR(p.realised_pnl(), 10.0, kTol);  // 1000 * (1.26 - 1.25)
    EXPECT_DOUBLE_EQ(p.average_price(), 0.0);
}

TEST(Portfolio, FeesReduceEquity) {
    bt::Portfolio p(100000.0);
    p.apply_fill(fill(bt::Side::Buy, 1000, 1.25, 2.5));
    p.mark_to_market(1.25);
    EXPECT_NEAR(p.equity(), 100000.0 - 2.5, kTol);
    EXPECT_NEAR(p.fees_paid(), 2.5, kTol);
}

TEST(Portfolio, MarkToMarketChangesEquityButNotCash) {
    bt::Portfolio p(100000.0);
    p.apply_fill(fill(bt::Side::Buy, 1000, 1.25));
    const double cash = p.cash();
    p.mark_to_market(1.30);
    EXPECT_DOUBLE_EQ(p.cash(), cash);
    EXPECT_NEAR(p.equity(), cash + 1300.0, kTol);
    EXPECT_NEAR(p.unrealised_pnl(), 50.0, kTol);  // 1000 * (1.30 - 1.25)
}

TEST(Portfolio, AddingKeepsAWeightedAveragePrice) {
    bt::Portfolio p(100000.0);
    p.apply_fill(fill(bt::Side::Buy, 1000, 1.0));
    p.apply_fill(fill(bt::Side::Buy, 3000, 2.0));
    EXPECT_NEAR(p.average_price(), (1000 * 1.0 + 3000 * 2.0) / 4000.0, kTol);  // 1.75
}

TEST(Portfolio, PartialCloseRealisesAgainstTheAverageAndKeepsIt) {
    bt::Portfolio p(100000.0);
    p.apply_fill(fill(bt::Side::Buy, 1000, 1.0));
    p.apply_fill(fill(bt::Side::Buy, 3000, 2.0));
    p.apply_fill(fill(bt::Side::Sell, 1000, 2.0));
    EXPECT_NEAR(p.realised_pnl(), 1000 * (2.0 - 1.75), kTol);
    EXPECT_NEAR(p.average_price(), 1.75, kTol);
    EXPECT_DOUBLE_EQ(p.position(), 3000.0);
}

TEST(Portfolio, FlipClosesThenOpensTheRemainderAtTheFillPrice) {
    bt::Portfolio p(100000.0);
    p.apply_fill(fill(bt::Side::Buy, 1000, 1.0));
    p.apply_fill(fill(bt::Side::Sell, 3000, 1.5));
    EXPECT_NEAR(p.realised_pnl(), 500.0, kTol);  // 1000 * (1.5 - 1.0)
    EXPECT_DOUBLE_EQ(p.position(), -2000.0);
    EXPECT_DOUBLE_EQ(p.average_price(), 1.5);
}

TEST(Portfolio, ShortPositionsProfitWhenPriceFalls) {
    bt::Portfolio p(100000.0);
    p.apply_fill(fill(bt::Side::Sell, 1000, 2.0));
    p.mark_to_market(1.8);
    EXPECT_NEAR(p.unrealised_pnl(), 200.0, kTol);
    p.apply_fill(fill(bt::Side::Buy, 1000, 1.5));
    EXPECT_NEAR(p.realised_pnl(), 500.0, kTol);
    EXPECT_NEAR(p.cash(), 100000.0 + 2000.0 - 1500.0, kTol);
}

TEST(Portfolio, EquityAlwaysEqualsCashPlusRealisedPlusUnrealisedMinusFees) {
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> price(0.9, 1.4), qty(1.0, 5000.0), fee(0.0, 3.0);
    std::bernoulli_distribution buy(0.5);
    bt::Portfolio p(100000.0);
    for (int i = 0; i < 2000; ++i) {
        p.apply_fill(fill(buy(rng) ? bt::Side::Buy : bt::Side::Sell, qty(rng), price(rng), fee(rng)));
        p.mark_to_market(price(rng));
        ASSERT_NEAR(p.equity(), p.initial_cash() + p.realised_pnl() + p.unrealised_pnl() - p.fees_paid(), 1e-6);
    }
}

TEST(Portfolio, RejectsNonsenseFills) {
    bt::Portfolio p(100000.0);
    EXPECT_THROW(p.apply_fill(fill(bt::Side::Buy, 0.0, 1.0)), std::invalid_argument);
    EXPECT_THROW(p.apply_fill(fill(bt::Side::Buy, 10.0, -1.0)), std::invalid_argument);
    EXPECT_THROW(p.apply_fill(fill(bt::Side::Buy, 10.0, 1.0, -0.5)), std::invalid_argument);
    EXPECT_THROW(bt::Portfolio(0.0), std::invalid_argument);
}

TEST(Costs, MidBasisPaysHalfTheSpreadEachWay) {
    bt::CostModel c;
    c.basis = bt::PriceBasis::Mid;
    c.spread = 0.0002;
    c.slippage = 0.00005;
    EXPECT_NEAR(bt::execution_price(c, bt::Side::Buy, 1.1000), 1.10015, 1e-12);
    EXPECT_NEAR(bt::execution_price(c, bt::Side::Sell, 1.1000), 1.09985, 1e-12);
}

TEST(Costs, BidBasisBuysAtTheAskAndSellsAtTheBid) {
    bt::CostModel c;
    c.basis = bt::PriceBasis::Bid;
    c.spread = 0.0001;
    c.slippage = 0.00002;
    EXPECT_NEAR(bt::execution_price(c, bt::Side::Buy, 1.1000), 1.10012, 1e-12);
    EXPECT_NEAR(bt::execution_price(c, bt::Side::Sell, 1.1000), 1.09998, 1e-12);
}

TEST(Costs, CommissionIsPerUnitPlusAShareOfNotional) {
    bt::CostModel c;
    c.commission_per_unit = 3.5 / 100000.0;  // $3.50 per 100k units
    c.commission_rate = 0.0001;               // 1 basis point
    EXPECT_NEAR(bt::commission(c, 100000.0, 1.10), 3.5 + 100000.0 * 1.10 * 0.0001, 1e-9);
}
