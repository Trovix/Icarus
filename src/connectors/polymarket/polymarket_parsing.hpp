#pragma once

#include <string>
#include <vector>

#include "connectors/polymarket/polymarket_models.hpp"
#include "core/market.hpp"
#include "core/orderbook.hpp"

namespace icarus::connectors::polymarket {

std::vector<RawMarket> parse_markets_json(const std::string& json);

RawMarket parse_market_json(const std::string& json);

RawOrderBookSide parse_order_book_json(const std::string& json);

icarus::core::Market to_canonical_market(const RawMarket& raw);

icarus::core::OrderBook to_canonical_order_book(
    const RawMarket& raw_market,
    const RawOrderBookSide& yes_book,
    const RawOrderBookSide& no_book
);

}  // namespace icarus::connectors::polymarket
