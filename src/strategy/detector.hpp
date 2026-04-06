#pragma once

#include <vector>

#include "core/orderbook.hpp"
#include "strategy/market_pair.hpp"
#include "strategy/opportunity.hpp"

namespace icarus::strategy {

std::vector<DetectedOpportunity> detect_opportunities(
    const MarketPair& pair,
    const icarus::core::OrderBook& kalshi_order_book,
    const icarus::core::OrderBook& polymarket_order_book
);

}  // namespace icarus::strategy
