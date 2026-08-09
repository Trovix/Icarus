#include <cassert>
#include <cmath>

#include "strategy/convergence.hpp"

namespace {

using icarus::core::Market;
using icarus::core::OrderBook;
using icarus::core::Venue;

void near(double actual, double expected) {
    assert(std::abs(actual - expected) < 1e-9);
}

Market market(Venue venue, const char* id) {
    return {venue, id, "Equivalent market", true};
}

OrderBook book(
    Venue venue,
    const char* id,
    double yes_bid,
    double yes_ask,
    double no_bid,
    double no_ask
) {
    OrderBook value{};
    value.venue = venue;
    value.venue_market_id = id;
    value.snapshot_time_unix_ms = 1000;
    value.yes_bids.levels = {{yes_bid, 100.0}};
    value.yes_asks.levels = {{yes_ask, 100.0}};
    value.no_bids.levels = {{no_bid, 100.0}};
    value.no_asks.levels = {{no_ask, 100.0}};
    return value;
}

void test_aligned_divergence_direction() {
    const icarus::strategy::MarketPair pair{
        market(Venue::Kalshi, "K"), market(Venue::Polymarket, "P"),
        "pair-1", true, 0.99,
    };
    const auto snapshot = icarus::strategy::build_convergence_snapshot(
        pair,
        book(Venue::Kalshi, "K", 0.68, 0.70, 0.30, 0.32),
        book(Venue::Polymarket, "P", 0.48, 0.50, 0.50, 0.52)
    );
    assert(snapshot);
    near(snapshot->spread, 0.20);
    near(snapshot->kalshi_no_polymarket_yes_entry_cost, 0.82);

    const auto signal = icarus::strategy::detect_convergence_entry(*snapshot, 0.10);
    assert(signal);
    assert(signal->direction ==
        icarus::strategy::OpportunityDirection::KalshiNoPolymarketYes);
    near(signal->combined_entry_ask, 0.82);
}

void test_inverse_polarity_uses_physical_no_as_canonical_yes() {
    const icarus::strategy::MarketPair pair{
        market(Venue::Kalshi, "K2"), market(Venue::Polymarket, "P2"),
        "pair-2", false, 0.98,
    };
    const auto snapshot = icarus::strategy::build_convergence_snapshot(
        pair,
        book(Venue::Kalshi, "K2", 0.28, 0.30, 0.70, 0.72),
        // Physical Polymarket NO represents canonical YES for this inverse pair.
        book(Venue::Polymarket, "P2", 0.38, 0.40, 0.60, 0.62)
    );
    assert(snapshot);
    near(snapshot->polymarket_yes_reference, 0.61);
    near(snapshot->spread, -0.32);
    // Kalshi YES + canonical Polymarket NO, which is physical YES.
    near(snapshot->kalshi_yes_polymarket_no_entry_cost, 0.70);

    const auto signal = icarus::strategy::detect_convergence_entry(*snapshot, 0.20);
    assert(signal);
    assert(signal->direction ==
        icarus::strategy::OpportunityDirection::KalshiYesPolymarketNo);
}

void test_rejects_small_spread_and_wrong_books() {
    const icarus::strategy::MarketPair pair{
        market(Venue::Kalshi, "K3"), market(Venue::Polymarket, "P3")
    };
    const auto snapshot = icarus::strategy::build_convergence_snapshot(
        pair,
        book(Venue::Kalshi, "K3", 0.48, 0.52, 0.48, 0.52),
        book(Venue::Polymarket, "P3", 0.47, 0.51, 0.49, 0.53)
    );
    assert(snapshot);
    assert(!icarus::strategy::detect_convergence_entry(*snapshot, 0.05));

    assert(!icarus::strategy::build_convergence_snapshot(
        pair,
        book(Venue::Kalshi, "WRONG", 0.48, 0.52, 0.48, 0.52),
        book(Venue::Polymarket, "P3", 0.47, 0.51, 0.49, 0.53)
    ));
}

}  // namespace

int main() {
    test_aligned_divergence_direction();
    test_inverse_polarity_uses_physical_no_as_canonical_yes();
    test_rejects_small_spread_and_wrong_books();
    return 0;
}
