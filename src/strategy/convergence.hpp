#pragma once

#include <optional>

#include "core/orderbook.hpp"
#include "strategy/market_pair.hpp"
#include "strategy/opportunity.hpp"

namespace icarus::strategy {

struct ConvergenceSnapshot {
    MarketPair pair;
    double kalshi_yes_reference = 0.0;
    double polymarket_yes_reference = 0.0;
    double spread = 0.0;
    double kalshi_yes_polymarket_no_entry_cost = 0.0;
    double kalshi_no_polymarket_yes_entry_cost = 0.0;
    double kalshi_yes_polymarket_no_exit_value = 0.0;
    double kalshi_no_polymarket_yes_exit_value = 0.0;
    std::int64_t snapshot_time_unix_ms = 0;
};

struct ConvergenceSignal {
    MarketPair pair;
    OpportunityDirection direction = OpportunityDirection::KalshiYesPolymarketNo;
    double spread = 0.0;
    double absolute_spread = 0.0;
    double combined_entry_ask = 0.0;
    double expected_convergence_value = 1.0;
    std::int64_t snapshot_time_unix_ms = 0;
};

std::optional<ConvergenceSnapshot> build_convergence_snapshot(
    const MarketPair& pair,
    const core::OrderBook& kalshi_order_book,
    const core::OrderBook& polymarket_order_book
);

std::optional<ConvergenceSignal> detect_convergence_entry(
    const ConvergenceSnapshot& snapshot,
    double entry_spread_threshold
);

}  // namespace icarus::strategy
