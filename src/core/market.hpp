#pragma once

#include <string>

namespace icarus::core {

// Supported venues for canonical market data.
enum class Venue {
    Polymarket,
    Kalshi
};

// Minimal cross-venue market representation.
struct Market {
    Venue venue;
    std::string venue_market_id;
    std::string title;
    bool active;
    bool closed;
};

}  // namespace icarus::core
