#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "market.hpp"

namespace icarus::core {

struct PriceLevel {
    double price;
    double size;
};

struct OrderBookSide {
    // Best price should appear first.
    std::vector<PriceLevel> levels;
};

struct OrderBook {
    Venue venue;
    std::string venue_market_id;
    std::int64_t snapshot_time_unix_ms;
    OrderBookSide yes_bids;
    OrderBookSide yes_asks;
    OrderBookSide no_bids;
    OrderBookSide no_asks;
};

}  // namespace icarus::core