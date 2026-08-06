#include "paper/paper_trading.hpp"
#include "strategy/convergence.hpp"

#include <cassert>
#include <cmath>
#include <map>

namespace {

using icarus::core::Market;
using icarus::core::OrderBook;
using icarus::core::Venue;
using icarus::paper::ConvergenceCloseRequest;
using icarus::paper::OpenConvergenceRequest;
using icarus::paper::PaperTradingConfig;
using icarus::paper::PaperTradingEngine;
using icarus::strategy::MarketPair;

OrderBook book(
    Venue venue,
    const char* id,
    std::int64_t time,
    double yes_bid,
    double yes_ask,
    double no_bid,
    double no_ask
) {
    OrderBook value;
    value.venue = venue;
    value.venue_market_id = id;
    value.snapshot_time_unix_ms = time;
    value.yes_bids.levels = {{yes_bid, 100.0}};
    value.yes_asks.levels = {{yes_ask, 100.0}};
    value.no_bids.levels = {{no_bid, 100.0}};
    value.no_asks.levels = {{no_ask, 100.0}};
    return value;
}

}  // namespace

int main() {
    const MarketPair pair{
        Market{Venue::Kalshi, "K-FLOW", "Equivalent fixture", true},
        Market{Venue::Polymarket, "P-FLOW", "Equivalent fixture", true},
        "fixture-flow", true, 0.99,
    };
    const auto kalshi_entry = book(
        Venue::Kalshi, "K-FLOW", 900, 0.34, 0.35, 0.64, 0.66
    );
    const auto polymarket_entry = book(
        Venue::Polymarket, "P-FLOW", 900, 0.59, 0.60, 0.39, 0.41
    );
    const auto divergent = icarus::strategy::build_convergence_snapshot(
        pair, kalshi_entry, polymarket_entry
    );
    assert(divergent);
    const auto signal = icarus::strategy::detect_convergence_entry(*divergent, 0.10);
    assert(signal);

    PaperTradingConfig config;
    config.risk.maximum_quote_age_ms = 1000;
    config.risk.maximum_trade_quantity = 10.0;
    config.risk.maximum_order_notional = 100.0;
    config.risk.maximum_total_exposure = 1000.0;
    config.risk.maximum_venue_exposure = 1000.0;
    config.risk.maximum_pair_quantity = 100.0;
    config.risk.maximum_open_positions = 10;
    PaperTradingEngine engine(
        config, {{Venue::Kalshi, 100.0}, {Venue::Polymarket, 100.0}}
    );

    OpenConvergenceRequest open;
    open.entry.request_id = "flow-entry";
    open.entry.pair = pair;
    open.entry.direction = signal->direction;
    open.entry.quantity = 10.0;
    open.entry.maximum_price_per_leg = 1.0;
    open.entry.signal_time_unix_ms = 1000;
    open.entry.kalshi_book = kalshi_entry;
    open.entry.polymarket_book = polymarket_entry;
    open.absolute_entry_spread = signal->absolute_spread;
    open.minimum_entry_spread = 0.10;
    open.exit_policy.minimum_combined_exit_bid = 0.98;
    const auto opened = engine.openConvergence(open);
    assert(opened.accepted);
    assert(engine.openTrades().size() == 1);

    const auto kalshi_exit = book(
        Venue::Kalshi, "K-FLOW", 1950, 0.48, 0.50, 0.50, 0.52
    );
    const auto polymarket_exit = book(
        Venue::Polymarket, "P-FLOW", 1950, 0.50, 0.52, 0.50, 0.52
    );
    const auto converged = icarus::strategy::build_convergence_snapshot(
        pair, kalshi_exit, polymarket_exit
    );
    assert(converged);
    assert(std::abs(converged->spread) < std::abs(divergent->spread));

    ConvergenceCloseRequest close;
    close.request_id = "flow-exit";
    close.lifecycle_id = engine.openTrades()[0].lifecycle_id;
    close.evaluation_time_unix_ms = 2000;
    close.kalshi_book = kalshi_exit;
    close.polymarket_book = polymarket_exit;
    const auto closed = engine.evaluateAndClose(close);
    assert(closed.triggered);
    assert(closed.fully_closed);
    assert(std::abs(closed.realized_pnl - 2.20) < 1e-9);

    const auto recovered = PaperTradingEngine::fromJson(engine.toJson());
    assert(recovered.toJson() == engine.toJson());
    assert(std::abs(recovered.realizedPnl() - 2.20) < 1e-9);
    return 0;
}
