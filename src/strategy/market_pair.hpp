#pragma once

#include <string>

#include "core/market.hpp"

namespace icarus::strategy {

struct MarketPair {
    icarus::core::Market kalshi;
    icarus::core::Market polymarket;
    std::string pair_id;
    bool outcomes_aligned = true;
    double match_confidence = 1.0;
};

}  // namespace icarus::strategy
