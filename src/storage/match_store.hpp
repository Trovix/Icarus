#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace icarus::storage {

struct MarketMatchIds {
    std::string kalshi_id;
    std::string polymarket_id;
};

std::vector<MarketMatchIds> load_market_matches(const std::filesystem::path& path);

void save_market_matches(
    const std::filesystem::path& path,
    const std::vector<MarketMatchIds>& matches
);

}  // namespace icarus::storage
