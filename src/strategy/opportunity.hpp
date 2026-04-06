#pragma once

#include <optional>

#include "strategy/market_pair.hpp"

namespace icarus::strategy {

struct OpportunitySnapshot {
    MarketPair pair;
    double kalshi_yes;
    double kalshi_no;
    double polymarket_yes;
    double polymarket_no;
    double yes_kalshi_no_polymarket_sum;
    double no_kalshi_yes_polymarket_sum;
    std::string polymarket_yes_token_id;
    std::string polymarket_no_token_id;
    std::string polymarket_yes_label;
    std::string polymarket_no_label;
};

struct Opportunity {
    MarketPair pair;
    double edge;
    double price_leg_1;
    double price_leg_2;
};

enum class OpportunityDirection {
    KalshiYesPolymarketNo,
    KalshiNoPolymarketYes,
};

struct DetectedOpportunity {
    Opportunity opportunity;
    OpportunityDirection direction;
};

std::optional<OpportunitySnapshot> build_opportunity_snapshot(
    const MarketPair& pair,
    const icarus::core::OrderBook& kalshi_order_book,
    const icarus::core::OrderBook& polymarket_order_book
);

}  // namespace icarus::strategy
