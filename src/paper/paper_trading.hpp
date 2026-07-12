#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "core/orderbook.hpp"
#include "strategy/opportunity.hpp"

namespace icarus::paper {

enum class Outcome { Yes, No };
enum class OrderSide { Buy, Sell };
enum class OrderStatus { Filled, PartiallyFilled, Rejected };
enum class TradeStatus { Filled, PartiallyFilled, Orphaned, Rejected };
enum class LifecycleStatus { Open, ClosingOrphan, Closed, Settled };
enum class ExitReason { None, Converged, ProfitTarget, StopLoss, MaximumHold, Forced };

struct VenueConfig {
    double proportional_fee_rate = 0.0;
    double fee_per_contract = 0.0;
    double slippage_buffer_per_contract = 0.0;
    std::int64_t simulated_latency_ms = 0;
};

struct RiskLimits {
    double minimum_net_edge = 0.0;
    double maximum_trade_quantity = 100.0;
    double maximum_order_notional = 1000.0;
    double maximum_total_exposure = 10000.0;
    double maximum_venue_exposure = 5000.0;
    double maximum_pair_quantity = 1000.0;
    double maximum_orphan_quantity = 100.0;
    std::size_t maximum_open_positions = 1000;
    std::int64_t maximum_quote_age_ms = 5000;
    std::int64_t cooldown_ms = 0;
};

struct PaperTradingConfig {
    VenueConfig kalshi;
    VenueConfig polymarket;
    RiskLimits risk;
};

struct BookFillLevel {
    double book_price = 0.0;
    double execution_price = 0.0;
    double quantity = 0.0;
};

struct BookWalk {
    double requested_quantity = 0.0;
    double filled_quantity = 0.0;
    double gross_notional = 0.0;
    double fee = 0.0;
    double average_execution_price = 0.0;
    std::vector<BookFillLevel> levels;
};

// Walks the ask side in supplied book order. Invalid levels are ignored. A
// finite limit applies to the buffered execution price, not the raw quote.
BookWalk walkBuyBook(
    const core::OrderBook& book,
    Outcome outcome,
    double requested_quantity,
    double limit_price,
    double maximum_debit,
    const VenueConfig& config
);

// Walks the bid side in supplied book order. Fees reduce cash proceeds but are
// reported separately from gross notional.
BookWalk walkSellBook(
    const core::OrderBook& book,
    Outcome outcome,
    double requested_quantity,
    double minimum_price,
    const VenueConfig& config
);

struct FillRecord {
    std::string fill_id;
    std::string order_id;
    core::Venue venue = core::Venue::Kalshi;
    std::string market_id;
    Outcome outcome = Outcome::Yes;
    OrderSide side = OrderSide::Buy;
    double quantity = 0.0;
    double book_price = 0.0;
    double execution_price = 0.0;
    double gross_notional = 0.0;
    double fee = 0.0;
    std::int64_t execution_time_unix_ms = 0;
};

struct OrderRecord {
    std::string order_id;
    std::string trade_id;
    std::string pair_key;
    core::Venue venue = core::Venue::Kalshi;
    std::string market_id;
    Outcome outcome = Outcome::Yes;
    OrderSide side = OrderSide::Buy;
    double requested_quantity = 0.0;
    double filled_quantity = 0.0;
    double limit_price = 1.0;
    double average_execution_price = 0.0;
    double gross_notional = 0.0;
    double fee = 0.0;
    std::int64_t submitted_time_unix_ms = 0;
    std::int64_t execution_time_unix_ms = 0;
    std::int64_t source_snapshot_time_unix_ms = 0;
    OrderStatus status = OrderStatus::Rejected;
    std::string rejection_reason;
};

struct OrphanExposure {
    core::Venue venue = core::Venue::Kalshi;
    std::string market_id;
    Outcome outcome = Outcome::Yes;
    double quantity = 0.0;
    double cost_basis = 0.0;
};

struct TradeRecord {
    std::string trade_id;
    std::string request_id;
    std::string pair_key;
    strategy::OpportunityDirection direction =
        strategy::OpportunityDirection::KalshiYesPolymarketNo;
    double requested_quantity = 0.0;
    double paired_quantity = 0.0;
    double estimated_locked_in_pnl = 0.0;
    std::int64_t signal_time_unix_ms = 0;
    std::vector<std::string> order_ids;
    std::optional<OrphanExposure> orphan;
    TradeStatus status = TradeStatus::Rejected;
    std::string rejection_reason;
};

struct Position {
    std::string pair_key;
    core::Venue venue = core::Venue::Kalshi;
    std::string market_id;
    Outcome outcome = Outcome::Yes;
    double quantity = 0.0;
    // Includes execution fees, so realized and unrealized P&L are net of fees.
    double cost_basis = 0.0;
};

struct SettlementRecord {
    std::string settlement_id;
    std::string pair_key;
    Outcome winning_outcome = Outcome::Yes;
    double payout = 0.0;
    double released_cost_basis = 0.0;
    double realized_pnl = 0.0;
    std::int64_t settlement_time_unix_ms = 0;
};

struct TwoLegRequest {
    std::string request_id;
    strategy::MarketPair pair;
    strategy::OpportunityDirection direction =
        strategy::OpportunityDirection::KalshiYesPolymarketNo;
    double quantity = 0.0;
    double maximum_price_per_leg = 1.0;
    std::int64_t signal_time_unix_ms = 0;
    core::OrderBook kalshi_book;
    core::OrderBook polymarket_book;
};

struct ConvergenceExitPolicy {
    // Exit when the two executable bid prices sum to at least this value.
    double minimum_combined_exit_bid = 0.98;
    // Absolute net P&L thresholds for the whole lifecycle.
    double profit_target = 0.0;
    double stop_loss = 0.0;
    std::int64_t maximum_hold_ms = 0;
};

struct OpenConvergenceRequest {
    TwoLegRequest entry;
    ConvergenceExitPolicy exit_policy;
    // Absolute probability dislocation measured by the signal layer after
    // applying the pair's polarity mapping. It is deliberately separate from
    // guaranteed-payout edge: convergence entries may cost more than $1.
    double absolute_entry_spread = 0.0;
    double minimum_entry_spread = 0.02;
};

struct OpenConvergenceTrade {
    std::string lifecycle_id;
    std::string entry_trade_id;
    std::string pair_key;
    strategy::OpportunityDirection direction =
        strategy::OpportunityDirection::KalshiYesPolymarketNo;
    std::string kalshi_market_id;
    std::string polymarket_market_id;
    Outcome kalshi_outcome = Outcome::Yes;
    Outcome polymarket_outcome = Outcome::No;
    double kalshi_open_quantity = 0.0;
    double polymarket_open_quantity = 0.0;
    double kalshi_cost_basis = 0.0;
    double polymarket_cost_basis = 0.0;
    double realized_pnl = 0.0;
    double absolute_entry_spread = 0.0;
    std::int64_t opened_time_unix_ms = 0;
    ConvergenceExitPolicy exit_policy;
    LifecycleStatus status = LifecycleStatus::Open;
    std::vector<std::string> closing_order_ids;
};

struct ConvergenceCloseRequest {
    std::string request_id;
    std::string lifecycle_id;
    std::int64_t evaluation_time_unix_ms = 0;
    core::OrderBook kalshi_book;
    core::OrderBook polymarket_book;
    double minimum_exit_price_per_leg = 0.0;
    bool force = false;
};

struct ConvergenceCloseResult {
    bool triggered = false;
    bool fully_closed = false;
    ExitReason reason = ExitReason::None;
    double realized_pnl = 0.0;
    std::optional<OrphanExposure> remaining_orphan;
    std::string message;
};

struct ExecutionResult {
    bool accepted = false;
    std::string reason;
    std::optional<TradeRecord> trade;
};

struct MarkPrice {
    std::string pair_key;
    core::Venue venue = core::Venue::Kalshi;
    std::string market_id;
    Outcome outcome = Outcome::Yes;
    double price = 0.0;
};

struct PortfolioSummary {
    double cash = 0.0;
    double open_cost_basis = 0.0;
    double realized_pnl = 0.0;
    double unrealized_pnl = 0.0;
    double locked_in_pnl = 0.0;
    double total_equity = 0.0;
};

std::string marketPairKey(const strategy::MarketPair& pair);

class PaperTradingEngine {
public:
    PaperTradingEngine(
        PaperTradingConfig config,
        std::map<core::Venue, double> starting_cash
    );

    ExecutionResult execute(const TwoLegRequest& request);
    ExecutionResult openConvergence(const OpenConvergenceRequest& request);
    ConvergenceCloseResult evaluateAndClose(const ConvergenceCloseRequest& request);
    SettlementRecord settle(
        const std::string& pair_key,
        Outcome winning_outcome,
        std::int64_t settlement_time_unix_ms
    );

    PortfolioSummary portfolio(const std::vector<MarkPrice>& marks = {}) const;
    double cash(core::Venue venue) const;
    double realizedPnl() const noexcept { return realized_pnl_; }

    const PaperTradingConfig& config() const noexcept { return config_; }
    const std::vector<OrderRecord>& orders() const noexcept { return orders_; }
    const std::vector<FillRecord>& fills() const noexcept { return fills_; }
    const std::vector<TradeRecord>& trades() const noexcept { return trades_; }
    const std::vector<Position>& positions() const noexcept { return positions_; }
    const std::vector<SettlementRecord>& settlements() const noexcept {
        return settlements_;
    }
    const std::vector<OpenConvergenceTrade>& openTrades() const noexcept {
        return open_trades_;
    }

    nlohmann::json toJson() const;
    static PaperTradingEngine fromJson(const nlohmann::json& json);

private:
    PaperTradingConfig config_;
    std::map<core::Venue, double> initial_cash_;
    std::map<core::Venue, double> cash_;
    std::vector<OrderRecord> orders_;
    std::vector<FillRecord> fills_;
    std::vector<TradeRecord> trades_;
    std::vector<Position> positions_;
    std::vector<SettlementRecord> settlements_;
    std::vector<OpenConvergenceTrade> open_trades_;
    std::map<std::string, std::int64_t> last_execution_by_pair_;
    std::vector<std::string> seen_request_ids_;
    double realized_pnl_ = 0.0;
    std::uint64_t next_order_id_ = 1;
    std::uint64_t next_fill_id_ = 1;
    std::uint64_t next_trade_id_ = 1;
    std::uint64_t next_settlement_id_ = 1;
    std::uint64_t next_lifecycle_id_ = 1;

    const VenueConfig& venueConfig(core::Venue venue) const;
    double totalExposure() const;
    double venueExposure(core::Venue venue) const;
    double pairQuantity(const std::string& pair_key) const;
    std::size_t openPositionCount() const;
    void applyOrderToPosition(const OrderRecord& order);
    ExecutionResult executeImpl(const TwoLegRequest& request, bool require_locked_in_edge);
};

}  // namespace icarus::paper
