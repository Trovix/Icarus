#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace icarus::connectors::polymarket {

// Minimal market fields pulled from Polymarket's Gamma API.
struct RawMarket {
    std::string id;
    std::string question;
    bool active;
    std::string description;
    std::string category;
    std::string rules;
    std::int64_t close_time_unix_ms;
    std::string yes_token_id;
    std::string no_token_id;
    std::string yes_outcome_label;
    std::string no_outcome_label;
    double best_yes_bid;
    double best_yes_ask;
    bool has_best_yes_bid;
    bool has_best_yes_ask;
    std::string result;
};

// Raw Polymarket price level before canonical mapping.
struct RawPriceLevel {
    double price;
    double size;
};

// Raw Polymarket book for a single token.
struct RawOrderBookSide {
    std::vector<RawPriceLevel> bids;
    std::vector<RawPriceLevel> asks;
};

}  // namespace icarus::connectors::polymarket
