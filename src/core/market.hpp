#pragma once

#include <string>

namespace icarus::core {

enum class Venue {
    Polymarket,
    Kalshi
};

struct Market {
    Venue venue;
    std::string venue_market_id;
    std::string title;
    bool active;
    bool closed;
};

}  // namespace icarus::core
