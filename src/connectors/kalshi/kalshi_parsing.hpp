#pragma once

#include <string>
#include <vector>

#include "connectors/kalshi/kalshi_models.hpp"
#include "core/market.hpp"
#include "core/orderbook.hpp"

namespace icarus::connectors::kalshi {

std::vector<RawMarket> parse_markets_json(const std::string& json);

RawOrderBook parse_order_book_json(const std::string& json);

icarus::core::Market to_canonical_market(const RawMarket& raw);

icarus::core::OrderBook to_canonical_order_book(const RawOrderBook& raw);

}  // namespace icarus::connectors::kalshi
