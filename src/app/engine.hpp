#pragma once

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "app/settings.hpp"
#include "connectors/http/http_client.hpp"
#include "connectors/kalshi/kalshi_connector.hpp"
#include "connectors/polymarket/polymarket_connector.hpp"
#include "core/market.hpp"
#include "paper/paper_trading.hpp"
#include "storage/match_store.hpp"
#include "strategy/convergence.hpp"
#include "strategy/market_pair.hpp"

namespace icarus::app {

class Engine {
public:
    explicit Engine(const std::filesystem::path& project_root);

    int run();

private:
    bool initialize();
    bool catalogs_need_refresh() const;
    bool refresh_catalogs_and_pairs();
    bool run_matcher() const;
    bool load_market_pairs(bool required);
    void maintain_market_pairs();
    void export_market_catalogs();
    void write_market_indexes(
        const std::vector<core::Market>& kalshi_markets,
        const std::vector<core::Market>& polymarket_markets
    ) const;
    std::vector<strategy::MarketPair> build_market_pairs(
        const std::vector<storage::MarketMatchIds>& match_ids
    );
    bool process_cycle();
    bool process_pair(const strategy::MarketPair& pair);
    bool try_settle_pair(const strategy::MarketPair& pair);
    void add_marks(
        const strategy::MarketPair& pair,
        const core::OrderBook& kalshi_book,
        const core::OrderBook& polymarket_book
    );
    void render_dashboard() const;
    void add_event(const std::string& message);
    bool handle_console_input();
    std::string next_request_id(const char* prefix);

    std::filesystem::path project_root_;
    std::filesystem::path settings_path_;
    std::filesystem::path paper_state_path_;
    std::filesystem::path match_store_path_;
    std::filesystem::path kalshi_catalog_path_;
    std::filesystem::path polymarket_catalog_path_;
    std::filesystem::path kalshi_index_path_;
    std::filesystem::path polymarket_index_path_;

    AppSettings settings_;
    connectors::HttpClient http_client_;
    connectors::kalshi::KalshiConnector kalshi_connector_;
    connectors::polymarket::PolymarketConnector polymarket_connector_;
    std::unique_ptr<paper::PaperTradingEngine> paper_engine_;
    std::vector<strategy::MarketPair> market_pairs_;
    std::vector<strategy::ConvergenceSnapshot> latest_snapshots_;
    std::vector<paper::MarkPrice> latest_marks_;
    std::vector<std::string> recent_events_;
    std::string venue_status_{"not connected"};
    bool paused_{false};
    bool running_{true};
    std::uint64_t request_sequence_{1};
    std::int64_t last_pair_reload_check_unix_ms_{0};
    std::filesystem::file_time_type match_store_write_time_{};
};

}  // namespace icarus::app
