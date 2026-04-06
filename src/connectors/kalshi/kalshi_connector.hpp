#pragma once

#include <string>
#include <vector>

#include "connectors/http/http_client.hpp"
#include "connectors/kalshi/kalshi_parsing.hpp"

namespace icarus::connectors::kalshi {

// Polling connector for Kalshi market and orderbook data.
class KalshiConnector {
public:
    explicit KalshiConnector(const HttpClient& http);

    std::vector<icarus::core::Market> fetch_markets();
    icarus::core::Market fetch_market(const std::string& ticker);

    icarus::core::OrderBook fetch_order_book(const std::string& ticker);

private:
    const HttpClient& http_; //http client shared between both platform's connectors it is not owned by this class
};

}  // namespace icarus::connectors::kalshi
