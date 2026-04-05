#include "connectors/kalshi/kalshi_parsing.hpp"

#include <nlohmann/json.hpp>

namespace icarus::connectors::kalshi {

namespace {

RawPriceLevel parse_price_level(const nlohmann::json& json) {
    RawPriceLevel level{};

    if (json.contains("price") && json["price"].is_number_integer()) {
        level.price = json["price"].get<int>();
    }

    if (json.contains("size") && json["size"].is_number_integer()) {
        level.size = json["size"].get<int>();
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
    const nlohmann::json parsed = nlohmann::json::parse(json);

    if (!parsed.is_array()) {
        return markets;
    }

    for (const nlohmann::json& item : parsed) {
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

        if (item.contains("active") && item["active"].is_boolean()) {
            market.active = item["active"].get<bool>();
        }

        if (item.contains("closed") && item["closed"].is_boolean()) {
            market.closed = item["closed"].get<bool>();
        }

        markets.push_back(market);
    }

    return markets;
}

RawOrderBook parse_order_book_json(const std::string& json) {
    RawOrderBook order_book{};
    const nlohmann::json parsed = nlohmann::json::parse(json);

    if (!parsed.is_object()) {
        return order_book;
    }

    if (parsed.contains("ticker") && parsed["ticker"].is_string()) {
        order_book.ticker = parsed["ticker"].get<std::string>();
    }

    if (parsed.contains("yes_bids") && parsed["yes_bids"].is_array()) {
        for (const nlohmann::json& level_json : parsed["yes_bids"]) {
            if (!level_json.is_object()) {
                continue;
            }

            order_book.yes_bids.push_back(parse_price_level(level_json));
        }
    }

    if (parsed.contains("no_bids") && parsed["no_bids"].is_array()) {
        for (const nlohmann::json& level_json : parsed["no_bids"]) {
            if (!level_json.is_object()) {
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
