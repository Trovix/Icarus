#include "connectors/kalshi/kalshi_connector.hpp"

#include <string>
#include <vector>

namespace icarus::connectors::kalshi {

KalshiConnector::KalshiConnector(const HttpClient& http)
    : http_(http) {}

std::vector<icarus::core::Market> KalshiConnector::fetch_markets() {
    std::vector<icarus::core::Market> markets;
    std::string cursor;

    while (true) {
        std::string url =
            "https://api.elections.kalshi.com/trade-api/v2/markets?limit=100";
        if (!cursor.empty()) {
            url += "&cursor=" + cursor;
        }

        const HttpResponse response = http_.get(url);
        if (response.status_code != 200) {
            return {};
        }

        const std::vector<RawMarket> raw_markets = parse_markets_json(response.body);
        for (const RawMarket& raw_market : raw_markets) {
            markets.push_back(to_canonical_market(raw_market));
        }

        cursor = parse_markets_cursor_json(response.body);
        if (cursor.empty()) {
            break;
        }
    }

    return markets;
}

icarus::core::Market KalshiConnector::fetch_market(const std::string& ticker) {
    const std::string url = "https://api.elections.kalshi.com/trade-api/v2/markets/" + ticker;
    const HttpResponse response = http_.get(url);

    if (response.status_code != 200) {
        return {};
    }

    const RawMarket raw_market = parse_market_json(response.body);
    return to_canonical_market(raw_market);
}

icarus::core::OrderBook KalshiConnector::fetch_order_book(const std::string& ticker) {
    const std::string url = "https://api.elections.kalshi.com/trade-api/v2/markets/" + ticker + "/orderbook";
    const HttpResponse response = http_.get(url);

    if (response.status_code != 200) {
        return {};
    }

    RawOrderBook raw_order_book = parse_order_book_json(response.body);
    // The requested ticker is known even if the order book payload omits it.
    raw_order_book.ticker = ticker;
    return to_canonical_order_book(raw_order_book);
}

}  // namespace icarus::connectors::kalshi
