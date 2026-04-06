#include "connectors/polymarket/polymarket_parsing.hpp"

#include <nlohmann/json.hpp>

namespace icarus::connectors::polymarket {

namespace {

std::vector<std::string> parse_token_ids(const nlohmann::json& json) {
    std::vector<std::string> token_ids;

    if (json.is_array()) {
        for (const nlohmann::json& item : json) {
            if (item.is_string()) {
                token_ids.push_back(item.get<std::string>());
            }
        }
        return token_ids;
    }

    if (json.is_string()) {
        const nlohmann::json parsed = nlohmann::json::parse(json.get<std::string>(), nullptr, false);
        if (!parsed.is_discarded()) {
            return parse_token_ids(parsed);
        }
    }

    return token_ids;
}

RawMarket parse_market(const nlohmann::json& item) {
    RawMarket market{};

    if (!item.is_object()) {
        return market;
    }

    if (item.contains("id") && item["id"].is_string()) {
        market.id = item["id"].get<std::string>();
    }

    if (item.contains("question") && item["question"].is_string()) {
        market.question = item["question"].get<std::string>();
    }

    if (item.contains("active") && item["active"].is_boolean()) {
        market.active = item["active"].get<bool>();
    }

    if (item.contains("clobTokenIds")) {
        const std::vector<std::string> token_ids = parse_token_ids(item["clobTokenIds"]);
        if (token_ids.size() >= 2) {
            market.yes_token_id = token_ids[0];
            market.no_token_id = token_ids[1];
        }
    }

    return market;
}

double parse_double(const nlohmann::json& json) {
    if (json.is_string()) {
        return std::stod(json.get<std::string>());
    }

    if (json.is_number()) {
        return json.get<double>();
    }

    return 0.0;
}

icarus::core::OrderBookSide to_canonical_side(const std::vector<RawPriceLevel>& raw_levels) {
    icarus::core::OrderBookSide side{};

    for (const RawPriceLevel& raw_level : raw_levels) {
        side.levels.push_back({raw_level.price, raw_level.size});
    }

    return side;
}

}  // namespace

std::vector<RawMarket> parse_markets_json(const std::string& json) {
    std::vector<RawMarket> markets;
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);

    if (parsed.is_discarded() || !parsed.is_array()) {
        return markets;
    }

    for (const nlohmann::json& item : parsed) {
        if (!item.is_object()) {
            continue;
        }

        RawMarket market = parse_market(item);
        if (!market.id.empty() && !market.question.empty() &&
            !market.yes_token_id.empty() && !market.no_token_id.empty()) {
            markets.push_back(market);
        }
    }

    return markets;
}

RawMarket parse_market_json(const std::string& json) {
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);

    if (parsed.is_discarded() || !parsed.is_object()) {
        return {};
    }

    return parse_market(parsed);
}

RawOrderBookSide parse_order_book_json(const std::string& json) {
    RawOrderBookSide order_book{};
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);

    if (parsed.is_discarded() || !parsed.is_object()) {
        return order_book;
    }

    if (parsed.contains("bids") && parsed["bids"].is_array()) {
        for (const nlohmann::json& level_json : parsed["bids"]) {
            if (!level_json.is_object()) {
                continue;
            }

            RawPriceLevel level{};
            if (level_json.contains("price")) {
                level.price = parse_double(level_json["price"]);
            }
            if (level_json.contains("size")) {
                level.size = parse_double(level_json["size"]);
            }
            order_book.bids.push_back(level);
        }
    }

    if (parsed.contains("asks") && parsed["asks"].is_array()) {
        for (const nlohmann::json& level_json : parsed["asks"]) {
            if (!level_json.is_object()) {
                continue;
            }

            RawPriceLevel level{};
            if (level_json.contains("price")) {
                level.price = parse_double(level_json["price"]);
            }
            if (level_json.contains("size")) {
                level.size = parse_double(level_json["size"]);
            }
            order_book.asks.push_back(level);
        }
    }

    return order_book;
}

icarus::core::Market to_canonical_market(const RawMarket& raw) {
    return {
        icarus::core::Venue::Polymarket,
        raw.id,
        raw.question,
        raw.active,
    };
}

icarus::core::OrderBook to_canonical_order_book(
    const RawMarket& raw_market,
    const RawOrderBookSide& yes_book,
    const RawOrderBookSide& no_book
) {
    icarus::core::OrderBook order_book{};
    order_book.venue = icarus::core::Venue::Polymarket;
    order_book.venue_market_id = raw_market.id;
    order_book.snapshot_time_unix_ms = 0;
    order_book.yes_bids = to_canonical_side(yes_book.bids);
    order_book.yes_asks = to_canonical_side(yes_book.asks);
    order_book.no_bids = to_canonical_side(no_book.bids);
    order_book.no_asks = to_canonical_side(no_book.asks);
    return order_book;
}

}  // namespace icarus::connectors::polymarket
