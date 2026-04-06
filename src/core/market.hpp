#pragma once

#include <string>

namespace icarus::core {

// Supported platforms 
enum class Venue {
    Polymarket,
    Kalshi
};

// Minimal cross-platform market representation. Should be enough to tag semantically linked pairs.
struct Market {
    Venue venue;
    std::string venue_market_id;
    std::string title;
    bool active; 
};

}  // namespace icarus::core
