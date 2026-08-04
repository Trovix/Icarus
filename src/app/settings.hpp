#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>

#include "paper/paper_trading.hpp"

namespace icarus::app {

struct AppSettings {
    paper::PaperTradingConfig paper;
    double starting_cash_kalshi = 10000.0;
    double starting_cash_polymarket = 10000.0;
    double trade_quantity = 10.0;
    double minimum_match_confidence = 0.90;
    double entry_spread_threshold = 0.08;
    double minimum_combined_exit_bid = 0.98;
    double profit_target = 1.0;
    double stop_loss = 2.0;
    std::int64_t maximum_hold_ms = 21600000;
    std::int64_t poll_interval_ms = 1000;
    std::int64_t pair_reload_interval_ms = 60000;
    std::int64_t catalog_refresh_interval_ms = 21600000;
    bool auto_match = true;
    bool allow_offline_matching = false;
};

AppSettings default_settings();
AppSettings load_settings(const std::filesystem::path& path);

}  // namespace icarus::app
