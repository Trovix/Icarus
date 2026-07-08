#include "strategy/convergence.hpp"

#include <algorithm>
#include <cmath>

#include "strategy/pricing.hpp"

namespace icarus::strategy {

namespace {

std::optional<double> midpoint(
    const std::optional<double>& bid,
    const std::optional<double>& ask
) {
    if (!bid || !ask || !std::isfinite(*bid) || !std::isfinite(*ask) ||
        *bid < 0.0 || *ask > 1.0 || *bid > *ask) {
        return std::nullopt;
    }
    return (*bid + *ask) / 2.0;
}

struct CanonicalTop {
    std::optional<double> yes_bid;
    std::optional<double> yes_ask;
    std::optional<double> no_bid;
    std::optional<double> no_ask;
};

CanonicalTop canonical_polymarket_top(
    const TopOfBook& physical,
    bool outcomes_aligned
) {
    if (outcomes_aligned) {
        return {
            physical.best_yes_bid,
            physical.best_yes_ask,
            physical.best_no_bid,
            physical.best_no_ask,
        };
    }
    return {
        physical.best_no_bid,
        physical.best_no_ask,
        physical.best_yes_bid,
        physical.best_yes_ask,
    };
}

bool finite_probability(double value) {
    return std::isfinite(value) && value >= 0.0 && value <= 1.0;
}

}  // namespace

std::optional<ConvergenceSnapshot> build_convergence_snapshot(
    const MarketPair& pair,
    const core::OrderBook& kalshi_order_book,
    const core::OrderBook& polymarket_order_book
) {
    if (kalshi_order_book.venue != core::Venue::Kalshi ||
        polymarket_order_book.venue != core::Venue::Polymarket ||
        kalshi_order_book.venue_market_id != pair.kalshi.venue_market_id ||
        polymarket_order_book.venue_market_id != pair.polymarket.venue_market_id) {
        return std::nullopt;
    }

    const TopOfBook kalshi = extract_top_of_book(kalshi_order_book);
    const CanonicalTop polymarket = canonical_polymarket_top(
        extract_top_of_book(polymarket_order_book), pair.outcomes_aligned
    );
    const std::optional<double> kalshi_yes = midpoint(
        kalshi.best_yes_bid, kalshi.best_yes_ask
    );
    const std::optional<double> polymarket_yes = midpoint(
        polymarket.yes_bid, polymarket.yes_ask
    );
    if (!kalshi_yes || !polymarket_yes || !kalshi.best_yes_ask ||
        !kalshi.best_no_ask || !kalshi.best_yes_bid || !kalshi.best_no_bid ||
        !polymarket.yes_ask || !polymarket.no_ask || !polymarket.yes_bid ||
        !polymarket.no_bid) {
        return std::nullopt;
    }

    const double values[] = {
        *kalshi.best_yes_ask,
        *kalshi.best_no_ask,
        *kalshi.best_yes_bid,
        *kalshi.best_no_bid,
        *polymarket.yes_ask,
        *polymarket.no_ask,
        *polymarket.yes_bid,
        *polymarket.no_bid,
    };
    if (!std::all_of(std::begin(values), std::end(values), finite_probability)) {
        return std::nullopt;
    }

    ConvergenceSnapshot snapshot;
    snapshot.pair = pair;
    snapshot.kalshi_yes_reference = *kalshi_yes;
    snapshot.polymarket_yes_reference = *polymarket_yes;
    snapshot.spread = *kalshi_yes - *polymarket_yes;
    snapshot.kalshi_yes_polymarket_no_entry_cost =
        *kalshi.best_yes_ask + *polymarket.no_ask;
    snapshot.kalshi_no_polymarket_yes_entry_cost =
        *kalshi.best_no_ask + *polymarket.yes_ask;
    snapshot.kalshi_yes_polymarket_no_exit_value =
        *kalshi.best_yes_bid + *polymarket.no_bid;
    snapshot.kalshi_no_polymarket_yes_exit_value =
        *kalshi.best_no_bid + *polymarket.yes_bid;
    snapshot.snapshot_time_unix_ms = std::min(
        kalshi_order_book.snapshot_time_unix_ms,
        polymarket_order_book.snapshot_time_unix_ms
    );
    return snapshot;
}

std::optional<ConvergenceSignal> detect_convergence_entry(
    const ConvergenceSnapshot& snapshot,
    double entry_spread_threshold
) {
    if (!std::isfinite(entry_spread_threshold) || entry_spread_threshold < 0.0 ||
        !std::isfinite(snapshot.spread)) {
        return std::nullopt;
    }

    const double absolute_spread = std::abs(snapshot.spread);
    if (absolute_spread < entry_spread_threshold) {
        return std::nullopt;
    }

    ConvergenceSignal signal;
    signal.pair = snapshot.pair;
    signal.spread = snapshot.spread;
    signal.absolute_spread = absolute_spread;
    signal.snapshot_time_unix_ms = snapshot.snapshot_time_unix_ms;
    if (snapshot.spread < 0.0) {
        signal.direction = OpportunityDirection::KalshiYesPolymarketNo;
        signal.combined_entry_ask = snapshot.kalshi_yes_polymarket_no_entry_cost;
    } else {
        signal.direction = OpportunityDirection::KalshiNoPolymarketYes;
        signal.combined_entry_ask = snapshot.kalshi_no_polymarket_yes_entry_cost;
    }
    return signal;
}

}  // namespace icarus::strategy

