#include "app/settings.hpp"

#include <fstream>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

namespace icarus::app {

namespace {

double non_negative_number(
    const nlohmann::json& object,
    const char* key,
    double fallback
) {
    if (!object.contains(key) || !object[key].is_number()) {
        return fallback;
    }
    const double value = object[key].get<double>();
    return value >= 0.0 ? value : fallback;
}

std::int64_t non_negative_integer(
    const nlohmann::json& object,
    const char* key,
    std::int64_t fallback
) {
    if (!object.contains(key) || !object[key].is_number_integer()) {
        return fallback;
    }
    const std::int64_t value = object[key].get<std::int64_t>();
    return value >= 0 ? value : fallback;
}

bool boolean_value(
    const nlohmann::json& object,
    const char* key,
    bool fallback
) {
    if (!object.contains(key) || !object[key].is_boolean()) {
        return fallback;
    }
    return object[key].get<bool>();
}

void load_venue_config(
    const nlohmann::json& object,
    paper::VenueConfig& config
) {
    if (!object.is_object()) {
        return;
    }
    config.proportional_fee_rate = non_negative_number(
        object, "proportional_fee_rate", config.proportional_fee_rate
    );
    config.fee_per_contract = non_negative_number(
        object, "fee_per_contract", config.fee_per_contract
    );
    config.slippage_buffer_per_contract = non_negative_number(
        object,
        "slippage_buffer_per_contract",
        config.slippage_buffer_per_contract
    );
    config.simulated_latency_ms = non_negative_integer(
        object, "simulated_latency_ms", config.simulated_latency_ms
    );
}

void load_risk_limits(
    const nlohmann::json& object,
    paper::RiskLimits& risk
) {
    if (!object.is_object()) {
        return;
    }
    risk.minimum_net_edge = non_negative_number(
        object, "minimum_net_edge", risk.minimum_net_edge
    );
    risk.maximum_trade_quantity = non_negative_number(
        object, "maximum_trade_quantity", risk.maximum_trade_quantity
    );
    risk.maximum_order_notional = non_negative_number(
        object, "maximum_order_notional", risk.maximum_order_notional
    );
    risk.maximum_total_exposure = non_negative_number(
        object, "maximum_total_exposure", risk.maximum_total_exposure
    );
    risk.maximum_venue_exposure = non_negative_number(
        object, "maximum_venue_exposure", risk.maximum_venue_exposure
    );
    risk.maximum_pair_quantity = non_negative_number(
        object, "maximum_pair_quantity", risk.maximum_pair_quantity
    );
    risk.maximum_orphan_quantity = non_negative_number(
        object, "maximum_orphan_quantity", risk.maximum_orphan_quantity
    );
    if (object.contains("maximum_open_positions") &&
        object["maximum_open_positions"].is_number_unsigned()) {
        risk.maximum_open_positions =
            object["maximum_open_positions"].get<std::size_t>();
    }
    risk.maximum_quote_age_ms = non_negative_integer(
        object, "maximum_quote_age_ms", risk.maximum_quote_age_ms
    );
    risk.cooldown_ms = non_negative_integer(
        object, "cooldown_ms", risk.cooldown_ms
    );
}

}  // namespace

AppSettings default_settings() {
    AppSettings settings;
    settings.paper.kalshi.proportional_fee_rate = 0.0;
    settings.paper.polymarket.proportional_fee_rate = 0.0;
    settings.paper.kalshi.slippage_buffer_per_contract = 0.005;
    settings.paper.polymarket.slippage_buffer_per_contract = 0.005;
    settings.paper.kalshi.simulated_latency_ms = 250;
    settings.paper.polymarket.simulated_latency_ms = 250;
    settings.paper.risk.minimum_net_edge = 0.01;
    settings.paper.risk.maximum_trade_quantity = 25.0;
    settings.paper.risk.maximum_order_notional = 1000.0;
    settings.paper.risk.maximum_total_exposure = 5000.0;
    settings.paper.risk.maximum_venue_exposure = 3000.0;
    settings.paper.risk.maximum_pair_quantity = 100.0;
    settings.paper.risk.maximum_orphan_quantity = 10.0;
    settings.paper.risk.maximum_open_positions = 100;
    settings.paper.risk.maximum_quote_age_ms = 10000;
    settings.paper.risk.cooldown_ms = 60000;
    return settings;
}

AppSettings load_settings(const std::filesystem::path& path) {
    AppSettings settings = default_settings();
    if (!std::filesystem::exists(path)) {
        return settings;
    }

    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("Unable to read settings: " + path.string());
    }

    const nlohmann::json root = nlohmann::json::parse(input, nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        throw std::runtime_error("Invalid settings JSON: " + path.string());
    }

    settings.starting_cash_kalshi = non_negative_number(
        root, "starting_cash_kalshi", settings.starting_cash_kalshi
    );
    settings.starting_cash_polymarket = non_negative_number(
        root, "starting_cash_polymarket", settings.starting_cash_polymarket
    );
    settings.trade_quantity = non_negative_number(
        root, "trade_quantity", settings.trade_quantity
    );
    settings.minimum_match_confidence = non_negative_number(
        root, "minimum_match_confidence", settings.minimum_match_confidence
    );
    settings.poll_interval_ms = non_negative_integer(
        root, "poll_interval_ms", settings.poll_interval_ms
    );
    settings.pair_reload_interval_ms = non_negative_integer(
        root,
        "pair_reload_interval_ms",
        settings.pair_reload_interval_ms
    );
    settings.catalog_refresh_interval_ms = non_negative_integer(
        root,
        "catalog_refresh_interval_ms",
        settings.catalog_refresh_interval_ms
    );
    settings.auto_match = boolean_value(root, "auto_match", settings.auto_match);
    settings.allow_offline_matching = boolean_value(
        root, "allow_offline_matching", settings.allow_offline_matching
    );

    if (root.contains("strategy") && root["strategy"].is_object()) {
        const nlohmann::json& strategy = root["strategy"];
        settings.entry_spread_threshold = non_negative_number(
            strategy, "entry_spread_threshold", settings.entry_spread_threshold
        );
        settings.minimum_combined_exit_bid = non_negative_number(
            strategy,
            "minimum_combined_exit_bid",
            settings.minimum_combined_exit_bid
        );
        settings.profit_target = non_negative_number(
            strategy, "profit_target", settings.profit_target
        );
        settings.stop_loss = non_negative_number(
            strategy, "stop_loss", settings.stop_loss
        );
        settings.maximum_hold_ms = non_negative_integer(
            strategy, "maximum_hold_ms", settings.maximum_hold_ms
        );
    }

    if (root.contains("venues") && root["venues"].is_object()) {
        const nlohmann::json& venues = root["venues"];
        if (venues.contains("kalshi")) {
            load_venue_config(venues["kalshi"], settings.paper.kalshi);
        }
        if (venues.contains("polymarket")) {
            load_venue_config(venues["polymarket"], settings.paper.polymarket);
        }
    }
    if (root.contains("risk")) {
        load_risk_limits(root["risk"], settings.paper.risk);
    }

    if (settings.trade_quantity <= 0.0 || settings.poll_interval_ms <= 0 ||
        settings.minimum_match_confidence > 1.0 ||
        settings.entry_spread_threshold > 1.0 ||
        settings.minimum_combined_exit_bid > 2.0) {
        throw std::runtime_error("Settings require positive trade quantity and poll interval");
    }
    return settings;
}

}  // namespace icarus::app
