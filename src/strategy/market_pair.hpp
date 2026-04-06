#pragma once

#include "core/market.hpp"

namespace icarus::strategy {

struct MarketPair {
    icarus::core::Market kalshi;
    icarus::core::Market polymarket;
};

}  // namespace icarus::strategy
