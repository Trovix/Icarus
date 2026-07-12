#include "paper/paper_trading.hpp"

#include <cmath>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>

namespace {

using icarus::core::Market;
using icarus::core::OrderBook;
using icarus::core::PriceLevel;
using icarus::core::Venue;
using icarus::paper::Outcome;
using icarus::paper::ConvergenceCloseRequest;
using icarus::paper::ConvergenceExitPolicy;
using icarus::paper::ExitReason;
using icarus::paper::LifecycleStatus;
using icarus::paper::OpenConvergenceRequest;
using icarus::paper::OrderSide;
using icarus::paper::PaperTradingConfig;
using icarus::paper::PaperTradingEngine;
using icarus::paper::TradeStatus;
using icarus::paper::TwoLegRequest;
using icarus::strategy::MarketPair;
using icarus::strategy::OpportunityDirection;

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void near(double actual, double expected, const std::string& message, double tolerance = 1e-8) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance) {
        ++failures;
        std::cerr << "FAIL: " << message << " (expected " << expected
                  << ", got " << actual << ")\n";
    }
}

MarketPair pair() {
    return {
        Market{Venue::Kalshi, "K-MARKET", "Will it happen?", true},
        Market{Venue::Polymarket, "P-MARKET", "Will it happen?", true}
    };
}

OrderBook book(
    Venue venue,
    const std::string& market_id,
    std::int64_t snapshot,
    std::initializer_list<PriceLevel> yes_asks,
    std::initializer_list<PriceLevel> no_asks
) {
    OrderBook value{};
    value.venue = venue;
    value.venue_market_id = market_id;
    value.snapshot_time_unix_ms = snapshot;
    value.yes_asks.levels = yes_asks;
    value.no_asks.levels = no_asks;
    return value;
}

PaperTradingConfig config() {
    PaperTradingConfig value;
    value.risk.minimum_net_edge = 0.01;
    value.risk.maximum_trade_quantity = 100.0;
    value.risk.maximum_order_notional = 1000.0;
    value.risk.maximum_total_exposure = 10000.0;
    value.risk.maximum_venue_exposure = 5000.0;
    value.risk.maximum_pair_quantity = 1000.0;
    value.risk.maximum_orphan_quantity = 100.0;
    value.risk.maximum_open_positions = 100;
    value.risk.maximum_quote_age_ms = 1000;
    return value;
}

TwoLegRequest request(
    std::string id,
    double quantity = 5.0,
    std::int64_t now = 10'000,
    double kalshi_yes = 0.40,
    double poly_no = 0.50
) {
    TwoLegRequest value;
    value.request_id = std::move(id);
    value.pair = pair();
    value.direction = OpportunityDirection::KalshiYesPolymarketNo;
    value.quantity = quantity;
    value.maximum_price_per_leg = 1.0;
    value.signal_time_unix_ms = now;
    value.kalshi_book = book(
        Venue::Kalshi, "K-MARKET", now - 100, {{kalshi_yes, quantity}}, {{0.55, quantity}});
    value.polymarket_book = book(
        Venue::Polymarket, "P-MARKET", now - 100, {{0.45, quantity}}, {{poly_no, quantity}});
    return value;
}

OpenConvergenceRequest openRequest(std::string id, double quantity = 5.0) {
    OpenConvergenceRequest value;
    value.entry = request(std::move(id), quantity);
    value.exit_policy.minimum_combined_exit_bid = 0.98;
    value.exit_policy.profit_target = 0.0;
    value.exit_policy.stop_loss = 0.0;
    value.exit_policy.maximum_hold_ms = 0;
    value.absolute_entry_spread = 0.20;
    value.minimum_entry_spread = 0.05;
    return value;
}

ConvergenceCloseRequest closeRequest(
    std::string id,
    std::string lifecycle_id,
    double kalshi_yes_bid,
    double poly_no_bid,
    std::int64_t now = 11'000
) {
    ConvergenceCloseRequest value;
    value.request_id = std::move(id);
    value.lifecycle_id = std::move(lifecycle_id);
    value.evaluation_time_unix_ms = now;
    value.kalshi_book = book(Venue::Kalshi, "K-MARKET", now - 100, {}, {});
    value.polymarket_book = book(Venue::Polymarket, "P-MARKET", now - 100, {}, {});
    value.kalshi_book.yes_bids.levels = {{kalshi_yes_bid, 100.0}};
    value.kalshi_book.no_bids.levels = {{0.50, 100.0}};
    value.polymarket_book.yes_bids.levels = {{0.45, 100.0}};
    value.polymarket_book.no_bids.levels = {{poly_no_bid, 100.0}};
    return value;
}

void testBookWalking() {
    const auto value = book(
        Venue::Kalshi, "K", 1, {{-1.0, 4.0}, {0.30, 2.0}, {0.40, 3.0}}, {});
    icarus::paper::VenueConfig fees;
    fees.proportional_fee_rate = 0.10;
    fees.fee_per_contract = 0.01;
    fees.slippage_buffer_per_contract = 0.01;
    const auto walked = icarus::paper::walkBuyBook(
        value, Outcome::Yes, 4.0, 0.50, std::numeric_limits<double>::max(), fees);
    near(walked.filled_quantity, 4.0, "book walker fills across levels");
    near(walked.gross_notional, 1.44, "buffer is included in gross notional");
    near(walked.average_execution_price, 0.36, "weighted average execution price");
    near(walked.fee, 0.184, "proportional and per-contract fees");
    check(walked.levels.size() == 2, "invalid book levels are ignored");

    const auto limited = icarus::paper::walkBuyBook(
        value, Outcome::Yes, 4.0, 0.35, 100.0, fees);
    near(limited.filled_quantity, 2.0, "limit price stops at the next level");

    const auto cash_limited = icarus::paper::walkBuyBook(
        value, Outcome::Yes, 4.0, 1.0, 0.362, fees);
    near(cash_limited.filled_quantity, 0.362 / 0.351,
         "maximum debit produces a deterministic fractional partial fill");

    auto bid_book = value;
    bid_book.yes_bids.levels = {{0.70, 2.0}, {0.60, 3.0}};
    const auto sold = icarus::paper::walkSellBook(
        bid_book, Outcome::Yes, 4.0, 0.50, fees);
    near(sold.filled_quantity, 4.0, "sell walker consumes bid depth");
    near(sold.gross_notional, 2.56, "sell slippage buffer reduces proceeds");
    near(sold.fee, 0.296, "sell fees are reported for net proceeds");
}

void testConvergenceOpenAndAccounting() {
    auto cfg = config();
    cfg.kalshi.proportional_fee_rate = 0.01;
    cfg.kalshi.simulated_latency_ms = 25;
    cfg.polymarket.fee_per_contract = 0.002;
    cfg.polymarket.simulated_latency_ms = 40;
    PaperTradingEngine engine(cfg, {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});

    const auto result = engine.openConvergence(openRequest("full"));
    check(result.accepted, "profitable convergence entry is accepted");
    check(result.trade && result.trade->status == TradeStatus::Filled, "trade is fully filled");
    near(result.trade->paired_quantity, 5.0, "paired quantity is recorded");
    near(result.trade->estimated_locked_in_pnl, 0.47, "locked P&L is net of fees");
    check(engine.orders().size() == 2, "two immutable order records are appended");
    check(engine.fills().size() == 2, "one fill record per consumed level");
    check(engine.positions().size() == 2, "both venue positions are tracked");
    check(engine.openTrades().size() == 1, "entry creates a persistent convergence lifecycle");
    check(engine.openTrades()[0].status == LifecycleStatus::Open,
          "fully paired convergence lifecycle starts open");
    near(engine.cash(Venue::Kalshi), 97.98, "Kalshi cash includes proportional fee");
    near(engine.cash(Venue::Polymarket), 97.49, "Polymarket cash includes per-contract fee");
    check(engine.orders()[0].execution_time_unix_ms == 10'025,
          "Kalshi latency is retained as execution metadata");
    check(engine.orders()[1].execution_time_unix_ms == 10'040,
          "Polymarket latency is retained as execution metadata");

    const auto key = icarus::paper::marketPairKey(pair());
    const auto summary = engine.portfolio({
        {key, Venue::Kalshi, "K-MARKET", Outcome::Yes, 0.42},
        {key, Venue::Polymarket, "P-MARKET", Outcome::No, 0.53}
    });
    near(summary.open_cost_basis, 4.53, "open cost basis includes fees");
    near(summary.unrealized_pnl, 0.22, "mark-to-market unrealized P&L");
    near(summary.locked_in_pnl, 0.47, "portfolio locked-in P&L");
    near(summary.total_equity, 200.22, "equity equals cash plus marked positions");
}

void testConvergenceExit() {
    PaperTradingEngine engine(config(), {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});
    const auto opened = engine.openConvergence(openRequest("open-convergence"));
    check(opened.accepted, "convergence position opens");
    const auto lifecycle_id = engine.openTrades()[0].lifecycle_id;

    const auto waiting = engine.evaluateAndClose(
        closeRequest("not-yet", lifecycle_id, 0.42, 0.50));
    check(!waiting.triggered, "position remains open before convergence");
    check(engine.orders().size() == 2, "non-triggering evaluation creates no orders");

    const auto closed = engine.evaluateAndClose(
        closeRequest("converged", lifecycle_id, 0.45, 0.53));
    check(closed.triggered && closed.reason == ExitReason::Converged,
          "combined executable bids trigger convergence exit");
    check(closed.fully_closed, "both convergence legs close through bid depth");
    near(closed.realized_pnl, 0.40, "close realizes proceeds less entry basis");
    near(engine.realizedPnl(), 0.40, "portfolio realized P&L updates on close");
    check(engine.openTrades()[0].status == LifecycleStatus::Closed,
          "lifecycle records terminal closed state");
    check(engine.orders()[2].side == OrderSide::Sell && engine.orders()[3].side == OrderSide::Sell,
          "closing orders are explicit sells");
    check(engine.fills()[2].side == OrderSide::Sell && engine.fills()[3].side == OrderSide::Sell,
          "closing fills are explicit sells");
    near(engine.portfolio().open_cost_basis, 0.0, "closed convergence trade has no open basis");
    near(engine.cash(Venue::Kalshi) + engine.cash(Venue::Polymarket), 200.40,
         "cash captures realized convergence profit");
}

void testConvergenceEntryAboveGuaranteedPayoutCost() {
    PaperTradingEngine legacy(config(), {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});
    auto convergence = openRequest("divergent-above-one");
    convergence.entry.kalshi_book.yes_asks.levels = {{0.52, 5.0}};
    convergence.entry.polymarket_book.no_asks.levels = {{0.52, 5.0}};
    convergence.absolute_entry_spread = 0.30;
    convergence.minimum_entry_spread = 0.10;
    check(!legacy.execute(convergence.entry).accepted,
          "legacy guaranteed-payout execution rejects combined entry cost above one");

    convergence.entry.request_id = "convergence-above-one";
    const auto opened = legacy.openConvergence(convergence);
    check(opened.accepted,
          "explicit divergence metric permits ordinary convergence entry above one");
    near(legacy.openTrades()[0].absolute_entry_spread, 0.30,
         "entry divergence is retained in lifecycle audit state");
    auto close = closeRequest(
        "convergence-above-one-close", legacy.openTrades()[0].lifecycle_id, 0.57, 0.55);
    const auto closed = legacy.evaluateAndClose(close);
    check(closed.triggered && closed.fully_closed,
          "above-one entry closes when executable bids converge through the target");
    near(closed.realized_pnl, 0.40,
         "above-one convergence lifecycle can close profitably before resolution");

    PaperTradingEngine weak(config(), {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});
    convergence.entry.request_id = "weak-divergence";
    convergence.absolute_entry_spread = 0.01;
    check(!weak.openConvergence(convergence).accepted,
          "convergence entry enforces its explicit minimum divergence");
}

void testConvergenceRiskExits() {
    {
        PaperTradingEngine engine(config(), {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});
        auto open = openRequest("profit-open");
        open.exit_policy.minimum_combined_exit_bid = 2.0;
        open.exit_policy.profit_target = 0.25;
        engine.openConvergence(open);
        const auto result = engine.evaluateAndClose(
            closeRequest("profit-close", engine.openTrades()[0].lifecycle_id, 0.45, 0.52));
        check(result.triggered && result.reason == ExitReason::ProfitTarget,
              "profit target triggers independently of convergence threshold");
    }
    {
        PaperTradingEngine engine(config(), {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});
        auto open = openRequest("stop-open");
        open.exit_policy.minimum_combined_exit_bid = 2.0;
        open.exit_policy.stop_loss = 0.50;
        engine.openConvergence(open);
        const auto result = engine.evaluateAndClose(
            closeRequest("stop-close", engine.openTrades()[0].lifecycle_id, 0.30, 0.40));
        check(result.triggered && result.reason == ExitReason::StopLoss,
              "executable stop loss triggers a risk exit");
        check(result.realized_pnl < -0.50, "stop loss realizes the executable loss");
    }
    {
        PaperTradingEngine engine(config(), {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});
        auto open = openRequest("hold-open");
        open.exit_policy.minimum_combined_exit_bid = 2.0;
        open.exit_policy.maximum_hold_ms = 500;
        engine.openConvergence(open);
        const auto result = engine.evaluateAndClose(
            closeRequest("hold-close", engine.openTrades()[0].lifecycle_id, 0.35, 0.45, 10'500));
        check(result.triggered && result.reason == ExitReason::MaximumHold,
              "maximum holding period triggers a risk exit");
    }
}

void testPartialConvergenceClose() {
    auto cfg = config();
    cfg.risk.maximum_orphan_quantity = 0.0;
    PaperTradingEngine engine(cfg, {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});
    engine.openConvergence(openRequest("partial-close-open"));
    auto close = closeRequest(
        "partial-close", engine.openTrades()[0].lifecycle_id, 0.45, 0.53);
    close.force = true;
    close.kalshi_book.yes_bids.levels = {{0.45, 2.0}};
    const auto result = engine.evaluateAndClose(close);
    check(result.triggered && !result.fully_closed, "thin bid depth produces a partial close");
    near(engine.openTrades()[0].kalshi_open_quantity, 3.0, "Kalshi lifecycle remainder");
    near(engine.openTrades()[0].polymarket_open_quantity, 3.0,
         "second-leg close is capped to avoid a new orphan");
    check(!result.remaining_orphan, "zero orphan allowance preserves a balanced remainder");
}

void testPartialFillAndOrphan() {
    auto cfg = config();
    cfg.risk.maximum_orphan_quantity = 10.0;
    PaperTradingEngine engine(cfg, {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});
    auto value = request("orphan", 5.0);
    value.polymarket_book.no_asks.levels = {{0.50, 2.0}};
    const auto result = engine.execute(value);
    check(result.accepted, "partially hedged trade is still an accepted paper execution");
    check(result.trade && result.trade->status == TradeStatus::Orphaned,
          "unequal independent fills are explicitly orphaned");
    near(result.trade->paired_quantity, 2.0, "paired portion of orphaned trade");
    check(result.trade->orphan.has_value(), "orphan exposure details are available");
    near(result.trade->orphan->quantity, 3.0, "orphan quantity is the fill imbalance");
    check(result.trade->orphan->venue == Venue::Kalshi &&
          result.trade->orphan->outcome == Outcome::Yes,
          "orphan identifies venue and outcome");
    check(engine.orders()[1].status == icarus::paper::OrderStatus::PartiallyFilled,
          "short-liquidity leg is recorded as a partial fill");
}

void testOrphanRiskBoundAndCash() {
    auto cfg = config();
    cfg.risk.maximum_orphan_quantity = 1.0;
    PaperTradingEngine engine(cfg, {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 0.50}});
    const auto result = engine.execute(request("bounded-orphan", 5.0));
    check(result.accepted, "cash-limited independent execution can proceed");
    check(result.trade->orphan.has_value(), "cash imbalance is exposed as an orphan");
    check(result.trade->orphan->quantity <= 1.0 + 1e-8,
          "pre-trade risk planning bounds orphan quantity");
    check(engine.cash(Venue::Polymarket) >= -1e-8, "venue cash never becomes negative");
}

void testRejectionsDedupAndCooldown() {
    auto cfg = config();
    cfg.risk.cooldown_ms = 500;
    PaperTradingEngine engine(cfg, {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});

    auto stale = request("stale");
    stale.kalshi_book.snapshot_time_unix_ms = 1;
    check(!engine.execute(stale).accepted, "stale books are rejected");

    auto weak = request("weak", 5.0, 10'000, 0.55, 0.50);
    check(!engine.execute(weak).accepted, "negative post-cost edge is rejected");

    const auto first = engine.execute(request("once"));
    check(first.accepted, "first unique request executes");
    check(!engine.execute(request("once", 5.0, 11'000)).accepted,
          "request IDs are idempotent even after cooldown");
    check(!engine.execute(request("cooldown", 5.0, 10'200)).accepted,
          "pair cooldown blocks a new request");
    check(engine.execute(request("after-cooldown", 5.0, 10'500)).accepted,
          "request executes at the cooldown boundary");

    auto oversized = request("oversized", 101.0, 12'000);
    check(!engine.execute(oversized).accepted, "maximum trade quantity is enforced");
}

void testSettlement() {
    PaperTradingEngine engine(config(), {{Venue::Kalshi, 10.0}, {Venue::Polymarket, 10.0}});
    const auto executed = engine.execute(request("settle", 5.0));
    const auto settled = engine.settle(
        executed.trade->pair_key, Outcome::Yes, 20'000);
    near(settled.payout, 5.0, "winning YES contracts pay one dollar");
    near(settled.released_cost_basis, 4.5, "settlement releases both cost bases");
    near(settled.realized_pnl, 0.5, "settlement realizes locked profit");
    near(engine.realizedPnl(), 0.5, "portfolio realized P&L is updated");
    near(engine.cash(Venue::Kalshi), 13.0, "winning venue receives settlement payout");
    near(engine.cash(Venue::Polymarket), 7.5, "losing venue receives no payout");
    near(engine.portfolio().open_cost_basis, 0.0, "settled positions are closed");

    bool threw = false;
    try {
        engine.settle(executed.trade->pair_key, Outcome::Yes, 21'000);
    } catch (const std::invalid_argument&) {
        threw = true;
    }
    check(threw, "a pair cannot be settled twice without new positions");
}

void testSnapshotRecovery() {
    auto cfg = config();
    cfg.risk.cooldown_ms = 1000;
    PaperTradingEngine original(cfg, {{Venue::Kalshi, 25.0}, {Venue::Polymarket, 30.0}});
    const auto executed = original.openConvergence(openRequest("persisted"));
    const auto snapshot = original.toJson();
    auto restored = PaperTradingEngine::fromJson(snapshot);

    check(restored.toJson() == snapshot, "JSON snapshot round-trips without information loss");
    near(restored.cash(Venue::Kalshi), original.cash(Venue::Kalshi),
         "cash balances survive recovery");
    near(restored.portfolio().locked_in_pnl, original.portfolio().locked_in_pnl,
         "positions and locked P&L survive recovery");
    check(restored.openTrades().size() == 1 &&
          restored.openTrades()[0].status == LifecycleStatus::Open,
          "open convergence lifecycle and exit policy survive recovery");
    check(!restored.execute(request("persisted", 5.0, 20'000)).accepted,
          "deduplication keys survive recovery");
    check(!restored.execute(request("new-but-cooling", 5.0, 10'500)).accepted,
          "cooldown state survives recovery");

    const auto settlement = restored.settle(executed.trade->pair_key, Outcome::No, 30'000);
    near(settlement.realized_pnl, 0.5, "restored positions settle correctly");
    check(settlement.settlement_id == "settlement-1", "ID counters survive recovery");
}

void testOppositeDirection() {
    PaperTradingEngine engine(config(), {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});
    auto value = request("opposite");
    value.direction = OpportunityDirection::KalshiNoPolymarketYes;
    value.kalshi_book.no_asks.levels = {{0.35, 5.0}};
    value.polymarket_book.yes_asks.levels = {{0.55, 5.0}};
    const auto result = engine.execute(value);
    check(result.accepted, "opposite complementary direction executes");
    check(engine.orders()[0].outcome == Outcome::No &&
          engine.orders()[1].outcome == Outcome::Yes,
          "direction maps to the correct outcomes");
}

void testPolarityAwareExecution() {
    PaperTradingEngine engine(config(), {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}});
    auto inverted = openRequest("inverted-polarity");
    inverted.entry.pair.outcomes_aligned = false;
    inverted.entry.direction = OpportunityDirection::KalshiYesPolymarketNo;
    inverted.entry.kalshi_book.yes_asks.levels = {{0.40, 5.0}};
    inverted.entry.polymarket_book.yes_asks.levels = {{0.50, 5.0}};
    inverted.entry.polymarket_book.no_asks.levels.clear();
    const auto result = engine.openConvergence(inverted);
    check(result.accepted, "inverted-outcome pair executes with polarity remapping");
    check(engine.orders()[0].outcome == Outcome::Yes &&
          engine.orders()[1].outcome == Outcome::Yes,
          "Kalshi YES uses Polymarket YES as the complementary semantic leg when inverted");
    check(engine.openTrades()[0].polymarket_outcome == Outcome::Yes,
          "lifecycle persists polarity-aware outcome selection");
}

}  // namespace

int main() {
    testBookWalking();
    testConvergenceOpenAndAccounting();
    testConvergenceExit();
    testConvergenceEntryAboveGuaranteedPayoutCost();
    testConvergenceRiskExits();
    testPartialConvergenceClose();
    testPartialFillAndOrphan();
    testOrphanRiskBoundAndCash();
    testRejectionsDedupAndCooldown();
    testSettlement();
    testSnapshotRecovery();
    testOppositeDirection();
    testPolarityAwareExecution();
    if (failures != 0) {
        std::cerr << failures << " paper-trading test(s) failed\n";
        return 1;
    }
    std::cout << "All paper-trading tests passed\n";
    return 0;
}
