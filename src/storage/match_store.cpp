#include "storage/match_store.hpp"

#include <fstream>

#include <nlohmann/json.hpp>

namespace icarus::storage {

std::vector<MarketMatchIds> load_market_matches(const std::filesystem::path& path) {
    std::vector<MarketMatchIds> matches;

    if (!std::filesystem::exists(path)) {
        return matches;
    }

    std::ifstream input(path);
    if (!input) {
        return matches;
    }

    nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_array()) {
        return matches;
    }

    for (const nlohmann::json& item : parsed) {
        if (!item.is_object()) {
            continue;
        }

        MarketMatchIds match{};
        if (item.contains("kalshi_id") && item["kalshi_id"].is_string()) {
            match.kalshi_id = item["kalshi_id"].get<std::string>();
        }
        if (item.contains("polymarket_id") && item["polymarket_id"].is_string()) {
            match.polymarket_id = item["polymarket_id"].get<std::string>();
        }

        if (!match.kalshi_id.empty() && !match.polymarket_id.empty()) {
            matches.push_back(match);
        }
    }

    return matches;
}

void save_market_matches(
    const std::filesystem::path& path,
    const std::vector<MarketMatchIds>& matches
) {
    std::filesystem::create_directories(path.parent_path());

    nlohmann::json output = nlohmann::json::array();
    for (const MarketMatchIds& match : matches) {
        output.push_back({
            {"kalshi_id", match.kalshi_id},
            {"polymarket_id", match.polymarket_id},
        });
    }

    std::ofstream file(path);
    file << output.dump(2) << '\n';
}

}  // namespace icarus::storage
