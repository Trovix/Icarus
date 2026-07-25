#include "connectors/kalshi/kalshi_parsing.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <initializer_list>
#include <nlohmann/json.hpp>

namespace icarus::connectors::kalshi {

namespace {

std::string string_field(
    const nlohmann::json& item,
    std::initializer_list<const char*> keys
) {
    for (const char* key : keys) {
        if (item.contains(key) && item[key].is_string()) {
            return item[key].get<std::string>();
        }
    }

    return {};
}

std::int64_t parse_iso8601_unix_ms(const std::string& value) {
    if (value.size() < 10) {
        return 0;
    }

    int year = 0;
    int month = 0;
    int day = 0;
    int hour = 0;
    int minute = 0;
    int second = 0;
    if (std::sscanf(
            value.c_str(),
            "%4d-%2d-%2dT%2d:%2d:%2d",
            &year,
            &month,
            &day,
            &hour,
            &minute,
            &second
        ) < 3) {
        return 0;
    }

    std::tm time{};
    time.tm_year = year - 1900;
    time.tm_mon = month - 1;
    time.tm_mday = day;
    time.tm_hour = hour;
    time.tm_min = minute;
    time.tm_sec = second;

#if defined(_WIN32)
    const std::int64_t epoch_seconds = static_cast<std::int64_t>(_mkgmtime64(&time));
#else
    const std::int64_t epoch_seconds = static_cast<std::int64_t>(timegm(&time));
#endif
    if (epoch_seconds < 0) {
        return 0;
    }

    std::int64_t milliseconds = 0;
    std::size_t timezone_position = value.size() >= 19 ? 19 : value.size();
    if (timezone_position < value.size() && value[timezone_position] == '.') {
        std::int64_t scale = 100;
        ++timezone_position;
        while (timezone_position < value.size() &&
               value[timezone_position] >= '0' && value[timezone_position] <= '9') {
            if (scale > 0) {
                milliseconds += (value[timezone_position] - '0') * scale;
                scale /= 10;
            }
            ++timezone_position;
        }
    }

    std::int64_t timezone_offset_ms = 0;
    if (timezone_position < value.size() &&
        (value[timezone_position] == '+' || value[timezone_position] == '-')) {
        const int direction = value[timezone_position] == '+' ? 1 : -1;
        int timezone_hour = 0;
        int timezone_minute = 0;
        if (std::sscanf(
                value.c_str() + timezone_position + 1,
                "%2d:%2d",
                &timezone_hour,
                &timezone_minute
            ) >= 1) {
            timezone_offset_ms = direction *
                static_cast<std::int64_t>(timezone_hour * 60 + timezone_minute) * 60 * 1000;
        }
    }

    return epoch_seconds * 1000 + milliseconds - timezone_offset_ms;
}

std::int64_t timestamp_field(
    const nlohmann::json& item,
    std::initializer_list<const char*> keys
) {
    for (const char* key : keys) {
        if (!item.contains(key) || item[key].is_null()) {
            continue;
        }

        if (item[key].is_string()) {
            const std::int64_t timestamp = parse_iso8601_unix_ms(item[key].get<std::string>());
            if (timestamp != 0) {
                return timestamp;
            }
        } else if (item[key].is_number_integer()) {
            const std::int64_t timestamp = item[key].get<std::int64_t>();
            return timestamp < 100000000000LL ? timestamp * 1000 : timestamp;
        }
    }

    return 0;
}

RawMarket parse_market(const nlohmann::json& item) {
    RawMarket market{};

    if (!item.is_object()) {
        return market;
    }

    if (item.contains("ticker") && item["ticker"].is_string()) {
        market.ticker = item["ticker"].get<std::string>();
    }

    market.title = string_field(item, {"title"});

    if (item.contains("status") && item["status"].is_string()) {
        const std::string status = item["status"].get<std::string>();
        // Collapse Kalshi's status string into the project's single active flag.
        market.active = (status == "open");
    } else if (item.contains("active") && item["active"].is_boolean()) {
        market.active = item["active"].get<bool>();
    }

    market.description = string_field(item, {"description", "subtitle"});
    market.category = string_field(item, {"category"});

    const std::string primary_rules = string_field(item, {"rules_primary", "rules"});
    const std::string secondary_rules = string_field(item, {"rules_secondary"});
    market.rules = primary_rules;
    if (!secondary_rules.empty()) {
        if (!market.rules.empty()) {
            market.rules += "\n\n";
        }
        market.rules += secondary_rules;
    }

    market.close_time_unix_ms = timestamp_field(
        item,
        {"close_time", "expected_expiration_time", "expiration_time", "latest_expiration_time"}
    );
    market.yes_outcome_label = string_field(item, {"yes_sub_title", "yes_outcome_label"});
    market.no_outcome_label = string_field(item, {"no_sub_title", "no_outcome_label"});
    if (market.yes_outcome_label.empty()) {
        market.yes_outcome_label = "Yes";
    }
    if (market.no_outcome_label.empty()) {
        market.no_outcome_label = "No";
    }
    market.result = string_field(item, {"result", "market_result"});
    std::transform(market.result.begin(), market.result.end(), market.result.begin(),
                   [](unsigned char character) {
                       return static_cast<char>(std::tolower(character));
                   });
    if (market.result != "yes" && market.result != "no") {
        market.result.clear();
    }

    return market;
}

// Kalshi returns prices as decimal dollars; convert to integer cents.
int parse_price_cents(const nlohmann::json& json) {
    try {
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
    } catch (...) {
        return 0;
    }

    return 0;
}

// Sizes may also arrive as strings, so normalize them to ints here.
int parse_size(const nlohmann::json& json) {
    try {
        if (json.is_string()) {
            return static_cast<int>(std::lround(std::stod(json.get<std::string>())));
        }

        if (json.is_number_float()) {
            return static_cast<int>(std::lround(json.get<double>()));
        }

        if (json.is_number_integer()) {
            return json.get<int>();
        }
    } catch (...) {
        return 0;
    }

    return 0;
}

// Support both array-shaped and object-shaped level payloads.
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

// Convert raw dollar prices into canonical probabilities in [0.0, 1.0].
// Kalshi orderbook_fp levels are returned in ascending price order, so the
// canonical side is reversed to keep the best level first.
icarus::core::OrderBookSide to_canonical_side(const std::vector<RawPriceLevel>& raw_levels) {
    icarus::core::OrderBookSide side{};

    for (auto it = raw_levels.rbegin(); it != raw_levels.rend(); ++it) {
        side.levels.push_back({
            static_cast<double>(it->price) / 100.0,
            static_cast<double>(it->size),
        });
    }

    return side;
}

// Binary asks are reconstructed from the opposing side's bids.
// Reversing the opposing bids keeps the implied ask ladder best-first too.
icarus::core::OrderBookSide to_reconstructed_ask_side(const std::vector<RawPriceLevel>& opposing_bids) {
    icarus::core::OrderBookSide side{};

    for (auto it = opposing_bids.rbegin(); it != opposing_bids.rend(); ++it) {
        side.levels.push_back({
            1.0 - (static_cast<double>(it->price) / 100.0),
            static_cast<double>(it->size),
        });
    }

    return side;
}

}  // namespace

std::vector<RawMarket> parse_markets_json(const std::string& json) {
    std::vector<RawMarket> markets;
    // Use non-throwing parsing so malformed responses become empty results.
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

        markets.push_back(parse_market(item));
    }

    return markets;
}

std::string parse_markets_cursor_json(const std::string& json) {
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);

    if (parsed.is_discarded() || !parsed.is_object()) {
        return {};
    }

    if (!parsed.contains("cursor") || !parsed["cursor"].is_string()) {
        return {};
    }

    return parsed["cursor"].get<std::string>();
}

RawMarket parse_market_json(const std::string& json) {
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);

    if (parsed.is_discarded() || !parsed.is_object()) {
        return {};
    }

    if (parsed.contains("market") && parsed["market"].is_object()) {
        return parse_market(parsed["market"]);
    }

    return parse_market(parsed);
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

    // Kalshi publishes binary bid ladders under orderbook_fp.
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
        raw.description,
        raw.category,
        raw.rules,
        raw.close_time_unix_ms,
        raw.yes_outcome_label,
        raw.no_outcome_label,
        raw.result,
    };
}

icarus::core::OrderBook to_canonical_order_book(const RawOrderBook& raw) {
    icarus::core::OrderBook order_book{};
    order_book.venue = icarus::core::Venue::Kalshi;
    order_book.venue_market_id = raw.ticker;
    order_book.snapshot_time_unix_ms = 0;
    // Kalshi exposes bids directly; asks are inferred from the opposite side.
    order_book.yes_bids = to_canonical_side(raw.yes_bids);
    order_book.yes_asks = to_reconstructed_ask_side(raw.no_bids);
    order_book.no_bids = to_canonical_side(raw.no_bids);
    order_book.no_asks = to_reconstructed_ask_side(raw.yes_bids);
    return order_book;
}

}  // namespace icarus::connectors::kalshi
