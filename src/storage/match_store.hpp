#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace icarus::storage {

struct MarketMatchIds {
    std::string kalshi_id;
    std::string polymarket_id;
    std::string pair_id;
    std::string yes_maps_to{"yes"};
    double confidence{1.0};
    std::string source;
    std::string reason;
};

std::vector<MarketMatchIds> load_market_matches(const std::filesystem::path& path);

void save_market_matches(
    const std::filesystem::path& path,
    const std::vector<MarketMatchIds>& matches
);

}  // namespace icarus::storage
