#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

#include "core/market.hpp"

namespace icarus::storage {

inline constexpr int kMarketCatalogSchemaVersion = 1;

struct MarketCatalog {
    int schema_version{kMarketCatalogSchemaVersion};
    std::int64_t generated_at_unix_ms{};
    std::vector<icarus::core::Market> markets;
};

// Malformed, unsupported, or unreadable catalogs produce an empty catalog.
MarketCatalog load_market_catalog(const std::filesystem::path& path);

// Writes via a sibling temporary file and atomically replaces the destination.
// Returns false if directories, serialization, or replacement fail.
bool save_market_catalog_atomic(
    const std::filesystem::path& path,
    const MarketCatalog& catalog
);

// Convenience export used by venue discovery. A zero generation timestamp is
// replaced with the current system time.
bool export_market_catalog_atomic(
    const std::filesystem::path& path,
    const std::vector<icarus::core::Market>& markets,
    std::int64_t generated_at_unix_ms = 0
);

}  // namespace icarus::storage
