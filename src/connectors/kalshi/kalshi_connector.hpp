#pragma once

#include <string>
#include <vector>

#include "connectors/http/http_client.hpp"
#include "connectors/kalshi/kalshi_parsing.hpp"

namespace icarus::connectors::kalshi {

// Polling connector for Kalshi market and order book data.
class KalshiConnector {
public:
    explicit KalshiConnector(const HttpClient& http);

    std::vector<icarus::core::Market> fetch_markets();

    icarus::core::OrderBook fetch_order_book(const std::string& ticker);

private:
    const HttpClient& http_;
};

}  // namespace icarus::connectors::kalshi
