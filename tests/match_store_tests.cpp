#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "storage/match_store.hpp"

namespace {

std::filesystem::path temporary_path(const std::string& suffix) {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    return std::filesystem::temp_directory_path() /
        ("icarus_match_store_" + std::to_string(unique) + suffix);
}

void test_loads_matcher_envelope_and_polarity() {
    const auto path = temporary_path(".json");
    {
        std::ofstream output(path);
        output << R"json({
          "schema_version": 1,
          "pairs": [
            {
              "kalshi_id": "K-1",
              "polymarket_id": "P-1",
              "yes_maps_to": "no",
              "confidence": 0.97,
              "source": "openai_judged",
              "reason": "Equivalent inverse propositions"
            },
            {
              "kalshi_id": "K-BAD",
              "polymarket_id": "P-BAD",
              "yes_maps_to": "maybe"
            }
          ]
        })json";
    }

    const auto matches = icarus::storage::load_market_matches(path);
    assert(matches.size() == 1);
    assert(matches[0].kalshi_id == "K-1");
    assert(matches[0].yes_maps_to == "no");
    assert(matches[0].confidence == 0.97);
    assert(!matches[0].pair_id.empty());

    std::error_code error;
    std::filesystem::remove(path, error);
}

void test_legacy_array_and_versioned_round_trip() {
    const auto legacy_path = temporary_path("_legacy.json");
    {
        std::ofstream output(legacy_path);
        output << R"json([{"kalshi_id":"K-2","polymarket_id":"P-2"}])json";
    }
    const auto legacy = icarus::storage::load_market_matches(legacy_path);
    assert(legacy.size() == 1);
    assert(legacy[0].yes_maps_to == "yes");

    const auto saved_path = temporary_path("_saved.json");
    icarus::storage::save_market_matches(saved_path, legacy);
    const auto saved = icarus::storage::load_market_matches(saved_path);
    assert(saved.size() == 1);
    assert(saved[0].kalshi_id == "K-2");
    assert(saved[0].confidence == 1.0);

    std::error_code error;
    std::filesystem::remove(legacy_path, error);
    error.clear();
    std::filesystem::remove(saved_path, error);
}

}  // namespace

int main() {
    test_loads_matcher_envelope_and_polarity();
    test_legacy_array_and_versioned_round_trip();
    return 0;
}
