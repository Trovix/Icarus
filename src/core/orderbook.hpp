#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "market.hpp"

namespace icarus::core {

// One quoted level on a binary order book side.
struct PriceLevel {
    double price;
    double size;
};

struct OrderBookSide {
    // Best price should appear first.
    std::vector<PriceLevel> levels;
};

// Canonical binary order book with explicit YES/NO bid and ask sides.
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
