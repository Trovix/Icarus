#pragma once

#include <filesystem>
#include <vector>

#include "connectors/http/http_client.hpp"
#include "connectors/kalshi/kalshi_connector.hpp"
#include "connectors/polymarket/polymarket_connector.hpp"
#include "core/market.hpp"
#include "storage/match_store.hpp"
#include "strategy/market_pair.hpp"
#include "strategy/opportunity.hpp"

namespace icarus::app {

class Engine {
public:
    explicit Engine(const std::filesystem::path& project_root);

    int run();

private:
    void export_market_indexes();
    void monitor_saved_pairs();
    void write_market_indexes(
        const std::vector<icarus::core::Market>& kalshi_markets,
        const std::vector<icarus::core::Market>& polymarket_markets
    ) const;
    void prompt_for_manual_pairs(std::vector<storage::MarketMatchIds>& match_ids);
    std::vector<strategy::MarketPair> build_market_pairs(
        const std::vector<storage::MarketMatchIds>& match_ids
    );
    std::vector<strategy::DetectedOpportunity> fetch_opportunities(
        const std::vector<strategy::MarketPair>& market_pairs
    );
    std::vector<strategy::OpportunitySnapshot> fetch_snapshots(
        const std::vector<strategy::MarketPair>& market_pairs
    );
    void print_opportunities(const std::vector<strategy::DetectedOpportunity>& opportunities) const;
    void print_snapshots(const std::vector<strategy::OpportunitySnapshot>& snapshots) const;

    std::filesystem::path project_root_;
    std::filesystem::path match_store_path_;
    std::filesystem::path kalshi_index_path_;
    std::filesystem::path polymarket_index_path_;
    connectors::HttpClient http_client_;
    connectors::kalshi::KalshiConnector kalshi_connector_;
    connectors::polymarket::PolymarketConnector polymarket_connector_;
};

}  // namespace icarus::app
