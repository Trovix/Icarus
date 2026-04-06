#pragma once 

#include <string> 
#include <vector>

#include "connectors/http/http_client.hpp"
#include "connectors/polymarket/polymarket_parsing.hpp"

namespace icarus::connectors::polymarket {

// Polling connector for Polymarket market and orderbook data
class PolymarketConnector {
public:
    explicit PolymarketConnector(const HttpClient& http);

    std::vector<icarus::core::Market> fetch_markets();

    icarus::core::OrderBook fetch_order_book(const std::string& ticker);
private:
    const HttpClient& http_; //http client shared between both platform's connectors it is not owned by this class
};



}