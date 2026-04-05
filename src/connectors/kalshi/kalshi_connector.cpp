#include "connectors/kalshi/kalshi_connector.hpp"

#include <string>
#include <vector>

namespace icarus::connectors::kalshi {

KalshiConnector::KalshiConnector(const HttpClient& http)
    : http_(http) {}

std::vector<icarus::core::Market> KalshiConnector::fetch_markets() {
    const std::string url = "https://api.kalshi.com/v1/markets";
    const HttpResponse response = http_.get(url);

    const std::vector<RawMarket> raw_markets = parse_markets_json(response.body);
    std::vector<icarus::core::Market> markets;
    markets.reserve(raw_markets.size());

    for (const RawMarket& raw_market : raw_markets) {
        markets.push_back(to_canonical_market(raw_market));
    }

    return markets;
}

icarus::core::OrderBook KalshiConnector::fetch_order_book(const std::string& ticker) {
    const std::string url = "https://api.kalshi.com/v1/markets/" + ticker + "/orderbook";
    const HttpResponse response = http_.get(url);
    const RawOrderBook raw_order_book = parse_order_book_json(response.body);
    return to_canonical_order_book(raw_order_book);
}

}  // namespace icarus::connectors::kalshi
