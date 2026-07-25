#include "storage/market_catalog.hpp"

#include <chrono>
#include <fstream>
#include <functional>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#include <nlohmann/json.hpp>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace icarus::storage {

namespace {

std::string venue_name(icarus::core::Venue venue) {
    switch (venue) {
        case icarus::core::Venue::Kalshi:
            return "kalshi";
        case icarus::core::Venue::Polymarket:
            return "polymarket";
    }

    return {};
}

bool parse_venue(const nlohmann::json& value, icarus::core::Venue& venue) {
    if (!value.is_string()) {
        return false;
    }

    const std::string name = value.get<std::string>();
    if (name == "kalshi") {
        venue = icarus::core::Venue::Kalshi;
        return true;
    }
    if (name == "polymarket") {
        venue = icarus::core::Venue::Polymarket;
        return true;
    }

    return false;
}

std::string optional_string(
    const nlohmann::json& item,
    const char* key,
    const std::string& fallback = {}
) {
    if (item.contains(key) && item[key].is_string()) {
        return item[key].get<std::string>();
    }
    return fallback;
}

nlohmann::json serialize_market(const icarus::core::Market& market) {
    return {
        {"venue", venue_name(market.venue)},
        {"venue_market_id", market.venue_market_id},
        {"title", market.title},
        {"active", market.active},
        {"description", market.description},
        {"category", market.category},
        {"rules", market.rules},
        {"close_time_unix_ms", market.close_time_unix_ms},
        {"result", market.result},
        {"outcomes", {
            {"yes", market.yes_outcome_label},
            {"no", market.no_outcome_label},
        }},
    };
}

bool parse_market(const nlohmann::json& item, icarus::core::Market& market) {
    if (!item.is_object() || !item.contains("venue") ||
        !parse_venue(item["venue"], market.venue)) {
        return false;
    }

    market.venue_market_id = optional_string(item, "venue_market_id");
    market.title = optional_string(item, "title");
    if (market.venue_market_id.empty() || market.title.empty()) {
        return false;
    }

    if (item.contains("active") && item["active"].is_boolean()) {
        market.active = item["active"].get<bool>();
    }
    market.description = optional_string(item, "description");
    market.category = optional_string(item, "category");
    market.rules = optional_string(item, "rules");
    market.result = optional_string(item, "result");
    if (item.contains("close_time_unix_ms") && item["close_time_unix_ms"].is_number_integer()) {
        market.close_time_unix_ms = item["close_time_unix_ms"].get<std::int64_t>();
    }

    if (item.contains("outcomes") && item["outcomes"].is_object()) {
        const nlohmann::json& outcomes = item["outcomes"];
        market.yes_outcome_label = optional_string(outcomes, "yes", "Yes");
        market.no_outcome_label = optional_string(outcomes, "no", "No");
    }

    return true;
}

std::int64_t current_time_unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::filesystem::path temporary_path_for(const std::filesystem::path& destination) {
    std::filesystem::path temporary = destination;
    const auto thread_hash = std::hash<std::thread::id>{}(std::this_thread::get_id());
    temporary += ".tmp." + std::to_string(current_time_unix_ms()) + "." +
        std::to_string(thread_hash);
    return temporary;
}

bool atomic_replace(
    const std::filesystem::path& temporary,
    const std::filesystem::path& destination
) {
#if defined(_WIN32)
    return MoveFileExW(
        temporary.c_str(),
        destination.c_str(),
        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
    ) != 0;
#else
    std::error_code error;
    std::filesystem::rename(temporary, destination, error);
    return !error;
#endif
}

}  // namespace

MarketCatalog load_market_catalog(const std::filesystem::path& path) {
    MarketCatalog catalog{};
    catalog.markets.clear();

    std::ifstream input(path);
    if (!input) {
        return catalog;
    }

    const nlohmann::json parsed = nlohmann::json::parse(input, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object() ||
        !parsed.contains("schema_version") || !parsed["schema_version"].is_number_integer() ||
        parsed["schema_version"].get<int>() != kMarketCatalogSchemaVersion ||
        !parsed.contains("markets") || !parsed["markets"].is_array()) {
        return catalog;
    }

    if (parsed.contains("generated_at_unix_ms") &&
        parsed["generated_at_unix_ms"].is_number_integer()) {
        catalog.generated_at_unix_ms = parsed["generated_at_unix_ms"].get<std::int64_t>();
    }

    for (const nlohmann::json& item : parsed["markets"]) {
        icarus::core::Market market{};
        if (parse_market(item, market)) {
            catalog.markets.push_back(std::move(market));
        }
    }

    return catalog;
}

bool save_market_catalog_atomic(
    const std::filesystem::path& path,
    const MarketCatalog& catalog
) {
    if (path.empty()) {
        return false;
    }

    std::error_code error;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, error);
        if (error) {
            return false;
        }
    }

    nlohmann::json markets = nlohmann::json::array();
    for (const icarus::core::Market& market : catalog.markets) {
        markets.push_back(serialize_market(market));
    }

    const nlohmann::json output = {
        {"schema_version", kMarketCatalogSchemaVersion},
        {"generated_at_unix_ms", catalog.generated_at_unix_ms},
        {"markets", std::move(markets)},
    };

    const std::filesystem::path temporary = temporary_path_for(path);
    {
        std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
        if (!file) {
            return false;
        }
        file << output.dump(2) << '\n';
        file.flush();
        if (!file) {
            file.close();
            std::filesystem::remove(temporary, error);
            return false;
        }
    }

    if (!atomic_replace(temporary, path)) {
        std::filesystem::remove(temporary, error);
        return false;
    }

    return true;
}

bool export_market_catalog_atomic(
    const std::filesystem::path& path,
    const std::vector<icarus::core::Market>& markets,
    std::int64_t generated_at_unix_ms
) {
    MarketCatalog catalog{};
    catalog.generated_at_unix_ms = generated_at_unix_ms == 0
        ? current_time_unix_ms()
        : generated_at_unix_ms;
    catalog.markets = markets;
    return save_market_catalog_atomic(path, catalog);
}

}  // namespace icarus::storage
