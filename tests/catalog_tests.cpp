#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "connectors/kalshi/kalshi_parsing.hpp"
#include "connectors/polymarket/polymarket_parsing.hpp"
#include "core/market.hpp"
#include "storage/market_catalog.hpp"

namespace {

void test_legacy_market_initialization() {
    const icarus::core::Market market{
        icarus::core::Venue::Kalshi,
        "LEGACY-1",
        "Legacy aggregate",
        true,
    };

    assert(market.active);
    assert(market.description.empty());
    assert(market.close_time_unix_ms == 0);
    assert(market.yes_outcome_label == "Yes");
    assert(market.no_outcome_label == "No");
}

void test_kalshi_metadata_parsing() {
    const std::string fixture = R"json({
        "market": {
            "ticker": "KX-WEATHER",
            "title": "Will it rain?",
            "subtitle": "Rain measured at Heathrow",
            "status": "open",
            "category": "Weather",
            "rules_primary": "The official station must report rain.",
            "rules_secondary": "Trace rainfall does not count.",
            "close_time": "2030-01-02T03:04:05.678Z",
            "yes_sub_title": "Rain",
            "no_sub_title": "No rain"
        }
    })json";

    const auto raw = icarus::connectors::kalshi::parse_market_json(fixture);
    const auto market = icarus::connectors::kalshi::to_canonical_market(raw);
    assert(market.venue == icarus::core::Venue::Kalshi);
    assert(market.venue_market_id == "KX-WEATHER");
    assert(market.description == "Rain measured at Heathrow");
    assert(market.category == "Weather");
    assert(market.rules ==
        "The official station must report rain.\n\nTrace rainfall does not count.");
    assert(market.close_time_unix_ms == 1893553445678LL);
    assert(market.yes_outcome_label == "Rain");
    assert(market.no_outcome_label == "No rain");

    // Bad numeric strings in unrelated order-book fields are ignored rather
    // than allowing venue payload drift to terminate discovery.
    const auto malformed_book = icarus::connectors::kalshi::parse_order_book_json(
        R"json({"orderbook_fp":{"yes_dollars":[["bad","bad"]],"no_dollars":[]}})json"
    );
    assert(malformed_book.yes_bids.size() == 1);
    assert(malformed_book.yes_bids[0].price == 0);
    assert(malformed_book.yes_bids[0].size == 0);
}

void test_polymarket_event_metadata_parsing() {
    const std::string fixture = R"json([{
        "description": "Settles from the official Met Office report.",
        "category": "Weather",
        "endDate": "2030-01-02T04:04:05+01:00",
        "markets": [{
            "id": "poly-1",
            "question": "Will it rain?",
            "active": true,
            "clobTokenIds": "[\"no-token\",\"yes-token\"]",
            "outcomes": "[\"No\",\"Yes\"]",
            "resolutionSource": "https://example.test/weather"
        }]
    }])json";

    const auto raw_markets =
        icarus::connectors::polymarket::parse_events_markets_json(fixture);
    assert(raw_markets.size() == 1);
    assert(raw_markets[0].yes_token_id == "yes-token");
    assert(raw_markets[0].no_token_id == "no-token");

    const auto market = icarus::connectors::polymarket::to_canonical_market(raw_markets[0]);
    assert(market.venue == icarus::core::Venue::Polymarket);
    assert(market.description == "Settles from the official Met Office report.");
    assert(market.category == "Weather");
    assert(market.rules == "Resolution source: https://example.test/weather");
    assert(market.close_time_unix_ms == 1893553445000LL);
    assert(market.yes_outcome_label == "Yes");
    assert(market.no_outcome_label == "No");
}

void test_catalog_round_trip_and_schema() {
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const std::filesystem::path path = std::filesystem::temp_directory_path() /
        ("icarus_market_catalog_" + std::to_string(unique) + ".json");

    std::vector<icarus::core::Market> markets{
        {
            icarus::core::Venue::Kalshi,
            "KX-1",
            "Will X happen?",
            true,
            "Description",
            "Politics",
            "Rule text",
            1893553445678LL,
            "Happens",
            "Does not happen",
        },
        {
            icarus::core::Venue::Polymarket,
            "POLY-1",
            "Will X happen?",
            false,
        },
    };

    assert(icarus::storage::export_market_catalog_atomic(path, markets, 123456789));

    std::ifstream raw_input(path);
    const nlohmann::json raw = nlohmann::json::parse(raw_input);
    assert(raw["schema_version"] == icarus::storage::kMarketCatalogSchemaVersion);
    assert(raw["generated_at_unix_ms"] == 123456789);
    assert(raw["markets"][0]["venue"] == "kalshi");
    assert(raw["markets"][0]["outcomes"]["yes"] == "Happens");
    raw_input.close();

    const auto loaded = icarus::storage::load_market_catalog(path);
    assert(loaded.schema_version == icarus::storage::kMarketCatalogSchemaVersion);
    assert(loaded.generated_at_unix_ms == 123456789);
    assert(loaded.markets.size() == 2);
    assert(loaded.markets[0].rules == "Rule text");
    assert(loaded.markets[0].yes_outcome_label == "Happens");
    assert(loaded.markets[1].venue == icarus::core::Venue::Polymarket);
    assert(loaded.markets[1].yes_outcome_label == "Yes");

    std::error_code error;
    std::filesystem::remove(path, error);
    assert(!error);
}

}  // namespace

int main() {
    test_legacy_market_initialization();
    test_kalshi_metadata_parsing();
    test_polymarket_event_metadata_parsing();
    test_catalog_round_trip_and_schema();
    return 0;
}
