#include "app/settings.hpp"
#include "storage/paper_store.hpp"

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>

namespace {

using icarus::app::load_settings;
using icarus::core::Venue;
using icarus::paper::PaperTradingEngine;
using icarus::storage::load_paper_state;
using icarus::storage::save_paper_state;

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        ++failures;
        std::cerr << "FAIL: " << message << '\n';
    }
}

void near(double actual, double expected, const std::string& message) {
    if (!std::isfinite(actual) || std::abs(actual - expected) > 1e-8) {
        ++failures;
        std::cerr << "FAIL: " << message << " (expected " << expected
                  << ", got " << actual << ")\n";
    }
}

class TemporaryDirectory {
public:
    TemporaryDirectory() {
        path_ = std::filesystem::temp_directory_path() /
            ("icarus-app-state-tests-" + std::to_string(++sequence_));
        std::filesystem::remove_all(path_);
        std::filesystem::create_directories(path_);
    }

    ~TemporaryDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path_, error);
    }

    const std::filesystem::path& path() const noexcept { return path_; }

private:
    static int sequence_;
    std::filesystem::path path_;
};

int TemporaryDirectory::sequence_ = 0;

void test_missing_settings_use_safe_defaults() {
    TemporaryDirectory temporary;
    const auto settings = load_settings(temporary.path() / "missing.json");

    near(settings.starting_cash_kalshi, 10000.0, "default Kalshi cash");
    near(settings.starting_cash_polymarket, 10000.0, "default Polymarket cash");
    near(settings.entry_spread_threshold, 0.08, "default divergence threshold");
    near(settings.minimum_combined_exit_bid, 0.98, "default convergence target");
    check(settings.auto_match, "automatic pair matching defaults on");
}

void test_settings_override_strategy_and_risk() {
    TemporaryDirectory temporary;
    const auto path = temporary.path() / "paper.json";
    {
        std::ofstream output(path);
        output << R"({
            "starting_cash_kalshi": 321.5,
            "trade_quantity": 7.0,
            "minimum_match_confidence": 0.95,
            "strategy": {
                "entry_spread_threshold": 0.12,
                "minimum_combined_exit_bid": 1.01,
                "profit_target": 3.5,
                "stop_loss": 1.25,
                "maximum_hold_ms": 60000
            },
            "risk": {
                "maximum_open_positions": 4,
                "maximum_total_exposure": 250.0
            }
        })";
    }

    const auto settings = load_settings(path);
    near(settings.starting_cash_kalshi, 321.5, "cash override");
    near(settings.trade_quantity, 7.0, "quantity override");
    near(settings.minimum_match_confidence, 0.95, "confidence override");
    near(settings.entry_spread_threshold, 0.12, "entry divergence override");
    near(settings.minimum_combined_exit_bid, 1.01, "exit convergence override");
    near(settings.profit_target, 3.5, "profit target override");
    near(settings.stop_loss, 1.25, "stop loss override");
    check(settings.maximum_hold_ms == 60000, "maximum hold override");
    check(settings.paper.risk.maximum_open_positions == 4, "position limit override");
    near(settings.paper.risk.maximum_total_exposure, 250.0, "exposure override");
}

void test_invalid_settings_are_rejected() {
    TemporaryDirectory temporary;
    const auto malformed = temporary.path() / "malformed.json";
    {
        std::ofstream output(malformed);
        output << "{not json";
    }

    bool threw = false;
    try {
        (void)load_settings(malformed);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "malformed settings are rejected");

    const auto invalid = temporary.path() / "invalid.json";
    {
        std::ofstream output(invalid);
        output << R"({"trade_quantity": 0})";
    }
    threw = false;
    try {
        (void)load_settings(invalid);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "zero trade quantity is rejected");
}

void test_paper_state_round_trip_and_absence() {
    TemporaryDirectory temporary;
    const auto path = temporary.path() / "nested" / "paper_state.json";
    check(!load_paper_state(path).has_value(), "missing state returns no portfolio");

    const auto settings = icarus::app::default_settings();
    PaperTradingEngine engine(
        settings.paper,
        {{Venue::Kalshi, 123.0}, {Venue::Polymarket, 456.0}}
    );
    save_paper_state(path, engine);

    check(std::filesystem::exists(path), "state file is created");
    check(!std::filesystem::exists(path.string() + ".tmp"), "temporary state is replaced");

    auto restored = load_paper_state(path);
    check(restored.has_value(), "saved state loads");
    near(restored->cash(Venue::Kalshi), 123.0, "Kalshi cash round-trips");
    near(restored->cash(Venue::Polymarket), 456.0, "Polymarket cash round-trips");
    check(restored->toJson() == engine.toJson(), "entire paper snapshot round-trips");
}

void test_corrupt_paper_state_is_rejected() {
    TemporaryDirectory temporary;
    const auto path = temporary.path() / "paper_state.json";
    {
        std::ofstream output(path);
        output << "not-json";
    }

    bool threw = false;
    try {
        (void)load_paper_state(path);
    } catch (const std::runtime_error&) {
        threw = true;
    }
    check(threw, "corrupt state is rejected rather than silently reset");
}

}  // namespace

int main() {
    test_missing_settings_use_safe_defaults();
    test_settings_override_strategy_and_risk();
    test_invalid_settings_are_rejected();
    test_paper_state_round_trip_and_absence();
    test_corrupt_paper_state_is_rejected();

    if (failures != 0) {
        std::cerr << failures << " app/state test(s) failed\n";
        return 1;
    }
    std::cout << "All app/state tests passed\n";
    return 0;
}
