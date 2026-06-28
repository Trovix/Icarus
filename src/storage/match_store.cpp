#include "storage/match_store.hpp"

#include <chrono>
#include <fstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

namespace icarus::storage {

namespace {

std::string optional_string(
    const nlohmann::json& object,
    const char* key,
    const std::string& fallback = {}
) {
    if (object.contains(key) && object[key].is_string()) {
        return object[key].get<std::string>();
    }
    return fallback;
}

std::int64_t current_time_unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

std::string default_pair_id(const MarketMatchIds& match) {
    return "kalshi:" + match.kalshi_id + "|polymarket:" + match.polymarket_id;
}

}  // namespace

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
    if (parsed.is_discarded()) {
        return matches;
    }

    // Preserve compatibility with the original bare-array store while loading
    // the versioned envelope emitted by the Python matcher.
    const nlohmann::json* items = nullptr;
    if (parsed.is_array()) {
        items = &parsed;
    } else if (parsed.is_object() && parsed.value("schema_version", 0) == 1 &&
               parsed.contains("pairs") && parsed["pairs"].is_array()) {
        items = &parsed["pairs"];
    }
    if (!items) {
        return matches;
    }

    for (const nlohmann::json& item : *items) {
        if (!item.is_object()) {
            continue;
        }

        MarketMatchIds match{};
        match.kalshi_id = optional_string(item, "kalshi_id");
        match.polymarket_id = optional_string(item, "polymarket_id");
        match.pair_id = optional_string(item, "pair_id");
        match.yes_maps_to = optional_string(item, "yes_maps_to", "yes");
        match.source = optional_string(item, "source");
        match.reason = optional_string(item, "reason");
        if (item.contains("confidence") && item["confidence"].is_number()) {
            match.confidence = item["confidence"].get<double>();
        }

        if (match.kalshi_id.empty() || match.polymarket_id.empty() ||
            (match.yes_maps_to != "yes" && match.yes_maps_to != "no") ||
            match.confidence < 0.0 || match.confidence > 1.0) {
            continue;
        }
        if (match.pair_id.empty()) {
            match.pair_id = default_pair_id(match);
        }
        matches.push_back(std::move(match));
    }

    return matches;
}

void save_market_matches(
    const std::filesystem::path& path,
    const std::vector<MarketMatchIds>& matches
) {
    std::filesystem::create_directories(path.parent_path());

    nlohmann::json pairs = nlohmann::json::array();
    for (const MarketMatchIds& match : matches) {
        const std::string pair_id = match.pair_id.empty()
            ? default_pair_id(match)
            : match.pair_id;
        pairs.push_back({
            {"kalshi_id", match.kalshi_id},
            {"polymarket_id", match.polymarket_id},
            {"pair_id", pair_id},
            {"yes_maps_to", match.yes_maps_to},
            {"confidence", match.confidence},
            {"source", match.source},
            {"reason", match.reason},
        });
    }

    std::ofstream file(path);
    if (!file) {
        throw std::runtime_error("Unable to write market matches: " + path.string());
    }
    file << nlohmann::json{
        {"schema_version", 1},
        {"generated_at_unix_ms", current_time_unix_ms()},
        {"pairs", std::move(pairs)},
    }.dump(2) << '\n';
}

}  // namespace icarus::storage
