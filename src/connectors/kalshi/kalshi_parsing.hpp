#pragma once

#include <string>
#include <vector>

#include "connectors/kalshi/kalshi_models.hpp"
#include "core/market.hpp"
#include "core/orderbook.hpp"

namespace icarus::connectors::kalshi {

// Parse the markets payload returned by Kalshi into raw connector models.
std::vector<RawMarket> parse_markets_json(const std::string& json);

// Parse a single Kalshi market payload into a raw connector model.
RawMarket parse_market_json(const std::string& json);

// Parse a single Kalshi order book payload into raw bid ladders.
RawOrderBook parse_order_book_json(const std::string& json);

// Map Kalshi's raw market fields into the canonical market type.
icarus::core::Market to_canonical_market(const RawMarket& raw);

// Map Kalshi's raw order book into the canonical YES/NO bid/ask layout.
icarus::core::OrderBook to_canonical_order_book(const RawOrderBook& raw);

}  // namespace icarus::connectors::kalshi
