#pragma once

#include <string>
#include <vector>

namespace icarus::connectors::kalshi {

struct RawMarket {
    std::string ticker;
    std::string title;
    bool active;
    bool closed;
};

struct RawPriceLevel {
    int price;
    int size;
};

struct RawOrderBook {
    std::string ticker;
    std::vector<RawPriceLevel> yes_bids;
    std::vector<RawPriceLevel> no_bids;
};

}  // namespace icarus::connectors::kalshi
