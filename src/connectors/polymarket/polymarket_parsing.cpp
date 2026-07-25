#include "connectors/polymarket/polymarket_parsing.hpp"

#include <cctype>
#include <cstdio>
#include <ctime>
#include <initializer_list>
#include <unordered_set>

#include <nlohmann/json.hpp>

namespace icarus::connectors::polymarket {

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

std::string to_lower(std::string value) {
    for (char& ch : value) {
        ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    }

    return value;
}

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

std::vector<std::string> parse_string_list(const nlohmann::json& json) {
    std::vector<std::string> values;

    if (json.is_array()) {
        for (const nlohmann::json& item : json) {
            if (item.is_string()) {
                values.push_back(item.get<std::string>());
            }
        }
        return values;
    }

    if (json.is_string()) {
        const nlohmann::json parsed = nlohmann::json::parse(json.get<std::string>(), nullptr, false);
        if (!parsed.is_discarded()) {
            return parse_string_list(parsed);
        }
    }

    return values;
}

double parse_double(const nlohmann::json& json) {
    try {
        if (json.is_string()) {
            return std::stod(json.get<std::string>());
        }

        if (json.is_number()) {
            return json.get<double>();
        }
    } catch (...) {
        return 0.0;
    }

    return 0.0;
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
    } else if (item.contains("closed") && item["closed"].is_boolean()) {
        market.active = !item["closed"].get<bool>();
    }

    market.description = string_field(item, {"description"});
    market.category = string_field(item, {"category", "eventCategory"});
    market.rules = string_field(item, {"rules", "resolutionRules", "resolution_rules"});
    const std::string resolution_source = string_field(item, {"resolutionSource", "resolution_source"});
    if (!resolution_source.empty()) {
        if (!market.rules.empty()) {
            market.rules += "\n\n";
        }
        market.rules += "Resolution source: " + resolution_source;
    }
    market.close_time_unix_ms = timestamp_field(
        item,
        {"endDate", "endDateIso", "end_date", "closeTime", "close_time"}
    );

    const std::vector<std::string> outcomes =
        item.contains("outcomes") ? parse_string_list(item["outcomes"]) : std::vector<std::string>{};
    const std::vector<std::string> short_outcomes =
        item.contains("shortOutcomes") ? parse_string_list(item["shortOutcomes"]) : std::vector<std::string>{};

    if (item.contains("bestBid") && !item["bestBid"].is_null()) {
        market.best_yes_bid = parse_double(item["bestBid"]);
        market.has_best_yes_bid = true;
    }

    if (item.contains("bestAsk") && !item["bestAsk"].is_null()) {
        market.best_yes_ask = parse_double(item["bestAsk"]);
        market.has_best_yes_ask = true;
    }

    if (item.contains("clobTokenIds")) {
        const std::vector<std::string> token_ids = parse_token_ids(item["clobTokenIds"]);

        if (token_ids.size() >= 2 && outcomes.size() == token_ids.size()) {
            for (std::size_t i = 0; i < token_ids.size(); ++i) {
                const std::string outcome = to_lower(outcomes[i]);
                if (outcome == "yes") {
                    market.yes_token_id = token_ids[i];
                    market.yes_outcome_label = outcomes[i];
                } else if (outcome == "no") {
                    market.no_token_id = token_ids[i];
                    market.no_outcome_label = outcomes[i];
                }
            }
        }

        if ((market.yes_token_id.empty() || market.no_token_id.empty()) &&
            short_outcomes.size() == token_ids.size()) {
            for (std::size_t i = 0; i < token_ids.size(); ++i) {
                const std::string outcome = to_lower(short_outcomes[i]);
                if (outcome == "yes") {
                    market.yes_token_id = token_ids[i];
                    market.yes_outcome_label = short_outcomes[i];
                } else if (outcome == "no") {
                    market.no_token_id = token_ids[i];
                    market.no_outcome_label = short_outcomes[i];
                }
            }
        }

        if (token_ids.size() >= 2 && market.yes_token_id.empty() && market.no_token_id.empty()) {
            market.yes_token_id = token_ids[0];
            market.no_token_id = token_ids[1];
            if (outcomes.size() >= 2) {
                market.yes_outcome_label = outcomes[0];
                market.no_outcome_label = outcomes[1];
            } else if (short_outcomes.size() >= 2) {
                market.yes_outcome_label = short_outcomes[0];
                market.no_outcome_label = short_outcomes[1];
            }
        }
    }

    if (market.yes_outcome_label.empty() || market.no_outcome_label.empty()) {
        const std::vector<std::string>& labels = outcomes.size() >= 2 ? outcomes : short_outcomes;
        if (labels.size() >= 2) {
            market.yes_outcome_label = labels[0];
            market.no_outcome_label = labels[1];
        }
    }
    if (market.yes_outcome_label.empty()) {
        market.yes_outcome_label = "Yes";
    }
    if (market.no_outcome_label.empty()) {
        market.no_outcome_label = "No";
    }

    const std::vector<std::string> outcome_prices = item.contains("outcomePrices")
        ? parse_string_list(item["outcomePrices"])
        : std::vector<std::string>{};
    if (outcome_prices.size() == outcomes.size()) {
        for (std::size_t i = 0; i < outcomes.size(); ++i) {
            try {
                if (std::stod(outcome_prices[i]) >= 1.0 - 1e-9) {
                    const std::string winning_label = to_lower(outcomes[i]);
                    if (winning_label == "yes" || winning_label == "no") {
                        market.result = winning_label;
                    }
                }
            } catch (const std::exception&) {
                // A malformed or unresolved price leaves the result empty.
            }
        }
    }

    return market;
}

icarus::core::OrderBookSide to_canonical_side_best_first(
    const std::vector<RawPriceLevel>& raw_levels
) {
    icarus::core::OrderBookSide side{};

    for (auto it = raw_levels.rbegin(); it != raw_levels.rend(); ++it) {
        const RawPriceLevel& raw_level = *it;
        side.levels.push_back({raw_level.price, raw_level.size});
    }

    return side;
}

void apply_synthetic_ask(
    icarus::core::OrderBookSide& ask_side,
    const std::vector<RawPriceLevel>& opposing_bids
) {
    if (opposing_bids.empty()) {
        return;
    }

    const RawPriceLevel& best_opposing_bid = opposing_bids.back();
    const icarus::core::PriceLevel synthetic_ask{
        1.0 - best_opposing_bid.price,
        best_opposing_bid.size,
    };

    if (ask_side.levels.empty() || synthetic_ask.price < ask_side.levels.front().price) {
        ask_side.levels.insert(ask_side.levels.begin(), synthetic_ask);
    }
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
        if (!market.id.empty() && !market.question.empty()) {
            markets.push_back(market);
        }
    }

    return markets;
}

std::vector<RawMarket> parse_events_markets_json(const std::string& json) {
    std::vector<RawMarket> markets;
    std::unordered_set<std::string> seen_market_ids;
    const nlohmann::json parsed = nlohmann::json::parse(json, nullptr, false);

    if (parsed.is_discarded() || !parsed.is_array()) {
        return markets;
    }

    for (const nlohmann::json& event_json : parsed) {
        if (!event_json.is_object()) {
            continue;
        }

        if (!event_json.contains("markets") || !event_json["markets"].is_array()) {
            continue;
        }

        for (const nlohmann::json& market_json : event_json["markets"]) {
            RawMarket market = parse_market(market_json);
            if (market.id.empty() || market.question.empty()) {
                continue;
            }

            if (seen_market_ids.insert(market.id).second) {
                if (market.description.empty()) {
                    market.description = string_field(event_json, {"description"});
                }
                if (market.category.empty()) {
                    market.category = string_field(event_json, {"category"});
                }
                if (market.rules.empty()) {
                    market.rules = string_field(
                        event_json,
                        {"rules", "resolutionRules", "resolution_rules"}
                    );
                }
                if (market.close_time_unix_ms == 0) {
                    market.close_time_unix_ms = timestamp_field(
                        event_json,
                        {"endDate", "endDateIso", "end_date", "closeTime", "close_time"}
                    );
                }
                markets.push_back(market);
            }
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
        raw.description,
        raw.category,
        raw.rules,
        raw.close_time_unix_ms,
        raw.yes_outcome_label,
        raw.no_outcome_label,
        raw.result,
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
    order_book.yes_bids = to_canonical_side_best_first(yes_book.bids);
    order_book.yes_asks = to_canonical_side_best_first(yes_book.asks);
    order_book.no_bids = to_canonical_side_best_first(no_book.bids);
    order_book.no_asks = to_canonical_side_best_first(no_book.asks);
    apply_synthetic_ask(order_book.yes_asks, no_book.bids);
    apply_synthetic_ask(order_book.no_asks, yes_book.bids);
    return order_book;
}

}  // namespace icarus::connectors::polymarket
