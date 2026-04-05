#include "connectors/kalshi/kalshi_parsing.hpp"

#include <cmath>
#include <nlohmann/json.hpp>

namespace icarus::connectors::kalshi {

namespace {

int parse_price_cents(const nlohmann::json& json) {
    if (json.is_string()) {
        const double dollars = std::stod(json.get<std::string>());
        return static_cast<int>(std::lround(dollars * 100.0));
    }

    if (json.is_number_float()) {
        return static_cast<int>(std::lround(json.get<double>() * 100.0));
    }

    if (json.is_number_integer()) {
        return json.get<int>();
    }

    return 0;
}

int parse_size(const nlohmann::json& json) {
    if (json.is_string()) {
        return static_cast<int>(std::lround(std::stod(json.get<std::string>())));
    }

    if (json.is_number_float()) {
        return static_cast<int>(std::lround(json.get<double>()));
    }

    if (json.is_number_integer()) {
        return json.get<int>();
    }

    return 0;
}

RawPriceLevel parse_price_level(const nlohmann::json& json) {
    RawPriceLevel level{};

    if (json.is_array() && json.size() >= 2) {
        level.price = parse_price_cents(json[0]);
        level.size = parse_size(json[1]);
        return level;
    }

    if (json.contains("price")) {
        level.price = parse_price_cents(json["price"]);
    }

    if (json.contains("size")) {
        level.size = parse_size(json["size"]);
    }

    return level;
}

icarus::core::OrderBookSide to_canonical_side(const std::vector<RawPriceLevel>& raw_levels) {
    icarus::core::OrderBookSide side{};

    for (const RawPriceLevel& raw_level : raw_levels) {
        side.levels.push_back({
            static_cast<double>(raw_level.price) / 100.0,
            static_cast<double>(raw_level.size),
        });
    }

    return side;
}

icarus::core::OrderBookSide to_reconstructed_ask_side(const std::vector<RawPriceLevel>& opposing_bids) {
    icarus::core::OrderBookSide side{};

    for (const RawPriceLevel& raw_level : opposing_bids) {
        side.levels.push_back({
            1.0 - (static_cast<double>(raw_level.price) / 100.0),
            static_cast<double>(raw_level.size),
        });
    }

    return side;
}

}  // namespace

std::vector<RawMarket> parse_markets_json(const std::string& json) {
    std::vector<RawMarket> markets;
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);

    if (parsed.is_discarded() || !parsed.is_object()) {
        return markets;
    }

    if (!parsed.contains("markets") || !parsed["markets"].is_array()) {
        return markets;
    }

    for (const nlohmann::json& item : parsed["markets"]) {
        if (!item.is_object()) {
            continue;
        }

        RawMarket market{};

        if (item.contains("ticker") && item["ticker"].is_string()) {
            market.ticker = item["ticker"].get<std::string>();
        }

        if (item.contains("title") && item["title"].is_string()) {
            market.title = item["title"].get<std::string>();
        }

        if (item.contains("status") && item["status"].is_string()) {
            const std::string status = item["status"].get<std::string>();
            market.active = (status == "open");
            market.closed = (status == "closed" || status == "settled");
        }

        markets.push_back(market);
    }

    return markets;
}

RawOrderBook parse_order_book_json(const std::string& json) {
    RawOrderBook order_book{};
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);

    if (parsed.is_discarded() || !parsed.is_object()) {
        return order_book;
    }

    if (!parsed.contains("orderbook_fp") || !parsed["orderbook_fp"].is_object()) {
        return order_book;
    }

    const nlohmann::json& orderbook_fp = parsed["orderbook_fp"];

    if (orderbook_fp.contains("yes_dollars") && orderbook_fp["yes_dollars"].is_array()) {
        for (const nlohmann::json& level_json : orderbook_fp["yes_dollars"]) {
            if (!level_json.is_array()) {
                continue;
            }

            order_book.yes_bids.push_back(parse_price_level(level_json));
        }
    }

    if (orderbook_fp.contains("no_dollars") && orderbook_fp["no_dollars"].is_array()) {
        for (const nlohmann::json& level_json : orderbook_fp["no_dollars"]) {
            if (!level_json.is_array()) {
                continue;
            }

            order_book.no_bids.push_back(parse_price_level(level_json));
        }
    }

    return order_book;
}

icarus::core::Market to_canonical_market(const RawMarket& raw) {
    return {
        icarus::core::Venue::Kalshi,
        raw.ticker,
        raw.title,
        raw.active,
        raw.closed,
    };
}

icarus::core::OrderBook to_canonical_order_book(const RawOrderBook& raw) {
    icarus::core::OrderBook order_book{};
    order_book.venue = icarus::core::Venue::Kalshi;
    order_book.venue_market_id = raw.ticker;
    order_book.snapshot_time_unix_ms = 0;
    order_book.yes_bids = to_canonical_side(raw.yes_bids);
    order_book.yes_asks = to_reconstructed_ask_side(raw.no_bids);
    order_book.no_bids = to_canonical_side(raw.no_bids);
    order_book.no_asks = to_reconstructed_ask_side(raw.yes_bids);
    return order_book;
}

}  // namespace icarus::connectors::kalshi
