#pragma once

#include <string>
#include <vector>

#include "connectors/http/http_client.hpp"
#include "connectors/polymarket/polymarket_parsing.hpp"

namespace icarus::connectors::polymarket {

// Polling connector for Polymarket market and order book data.
class PolymarketConnector {
public:
    explicit PolymarketConnector(const HttpClient& http);

    std::vector<icarus::core::Market> fetch_markets();
    icarus::core::Market fetch_market(const std::string& market_id);

    icarus::core::OrderBook fetch_order_book(const std::string& market_id);

private:
    const HttpClient& http_;
};

}  // namespace icarus::connectors::polymarket
