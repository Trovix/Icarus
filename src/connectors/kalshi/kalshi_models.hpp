#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace icarus::connectors::kalshi {

// Minimal market fields pulled directly from Kalshi's API response.
struct RawMarket {
    std::string ticker;
    std::string title;
    bool active;
    std::string description;
    std::string category;
    std::string rules;
    std::int64_t close_time_unix_ms;
    std::string yes_outcome_label;
    std::string no_outcome_label;
    std::string result;
};

// Raw Kalshi price level before canonical probability conversion.
struct RawPriceLevel {
    int price;
    int size;
};

// Raw Kalshi order book containing only the published bid ladders.
struct RawOrderBook {
    std::string ticker;
    std::vector<RawPriceLevel> yes_bids;
    std::vector<RawPriceLevel> no_bids;
};

}  // namespace icarus::connectors::kalshi
