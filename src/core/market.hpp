#pragma once

#include <cstdint>
#include <string>

namespace icarus::core {

// Supported platforms 
enum class Venue {
    Polymarket,
    Kalshi
};

// Cross-platform market metadata used by matching and trading. The original
// four fields intentionally remain first so existing aggregate initializers
// continue to compile as richer metadata is added.
struct Market {
    Venue venue{};
    std::string venue_market_id;
    std::string title;
    bool active{};
    std::string description;
    std::string category;
    std::string rules;
    std::int64_t close_time_unix_ms{};
    std::string yes_outcome_label{"Yes"};
    std::string no_outcome_label{"No"};
};

}  // namespace icarus::core
