#include "connectors/polymarket/polymarket_connector.hpp"

#include <string>
#include <vector>

namespace icarus::connectors::polymarket {

namespace {

RawMarket find_market_by_id(const std::vector<RawMarket>& markets, const std::string& market_id) {
    for (const RawMarket& market : markets) {
        if (market.id == market_id) {
            return market;
        }
    }

    return {};
}

}  // namespace

PolymarketConnector::PolymarketConnector(const HttpClient& http)
    : http_(http) {}

std::vector<icarus::core::Market> PolymarketConnector::fetch_markets() {
    std::vector<icarus::core::Market> markets;
    constexpr int page_size = 100;
    int offset = 0;

    while (true) {
        const std::string url =
            "https://gamma-api.polymarket.com/events?active=true&closed=false&limit=" +
            std::to_string(page_size) + "&offset=" + std::to_string(offset);
        const HttpResponse response = http_.get(url);

        if (response.status_code != 200) {
            return {};
        }

        const std::vector<RawMarket> raw_markets = parse_events_markets_json(response.body);
        for (const RawMarket& raw_market : raw_markets) {
            markets.push_back(to_canonical_market(raw_market));
        }

        if (raw_markets.size() < static_cast<std::size_t>(page_size)) {
            break;
        }

        offset += page_size;
    }

    return markets;
}

icarus::core::Market PolymarketConnector::fetch_market(const std::string& market_id) {
    const std::string url = "https://gamma-api.polymarket.com/markets/" + market_id;
    const HttpResponse response = http_.get(url);

    if (response.status_code != 200) {
        return {};
    }

    const RawMarket raw_market = parse_market_json(response.body);
    return to_canonical_market(raw_market);
}

icarus::core::OrderBook PolymarketConnector::fetch_order_book(const std::string& market_id) {
    const std::string market_url = "https://gamma-api.polymarket.com/markets/" + market_id;
    const HttpResponse market_response = http_.get(market_url);

    if (market_response.status_code != 200) {
        return {};
    }

    const RawMarket raw_market = parse_market_json(market_response.body);

    if (raw_market.id.empty()) {
        return {};
    }

    if (raw_market.has_best_yes_bid || raw_market.has_best_yes_ask) {
        RawOrderBookSide yes_book{};
        RawOrderBookSide no_book{};

        if (raw_market.has_best_yes_bid) {
            yes_book.bids.push_back({raw_market.best_yes_bid, 0.0});
            no_book.asks.push_back({1.0 - raw_market.best_yes_bid, 0.0});
        }

        if (raw_market.has_best_yes_ask) {
            yes_book.asks.push_back({raw_market.best_yes_ask, 0.0});
            no_book.bids.push_back({1.0 - raw_market.best_yes_ask, 0.0});
        }

        return to_canonical_order_book(raw_market, yes_book, no_book);
    }

    if (raw_market.yes_token_id.empty() || raw_market.no_token_id.empty()) {
        return {};
    }

    const std::string yes_book_url =
        "https://clob.polymarket.com/book?token_id=" + raw_market.yes_token_id;
    const std::string no_book_url =
        "https://clob.polymarket.com/book?token_id=" + raw_market.no_token_id;

    const HttpResponse yes_book_response = http_.get(yes_book_url);
    const HttpResponse no_book_response = http_.get(no_book_url);

    if (yes_book_response.status_code != 200 || no_book_response.status_code != 200) {
        return {};
    }

    const RawOrderBookSide yes_book = parse_order_book_json(yes_book_response.body);
    const RawOrderBookSide no_book = parse_order_book_json(no_book_response.body);
    return to_canonical_order_book(raw_market, yes_book, no_book);
}

}  // namespace icarus::connectors::polymarket
