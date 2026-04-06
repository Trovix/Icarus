#include "strategy/detector.hpp"

#include "strategy/pricing.hpp"

namespace icarus::strategy {

std::optional<OpportunitySnapshot> build_opportunity_snapshot(
    const MarketPair& pair,
    const icarus::core::OrderBook& kalshi_order_book,
    const icarus::core::OrderBook& polymarket_order_book
) {
    const TopOfBook kalshi_top = extract_top_of_book(kalshi_order_book);
    const TopOfBook polymarket_top = extract_top_of_book(polymarket_order_book);

    if (!kalshi_top.best_yes_ask.has_value() ||
        !kalshi_top.best_no_ask.has_value() ||
        !polymarket_top.best_yes_ask.has_value() ||
        !polymarket_top.best_no_ask.has_value()) {
        return std::nullopt;
    }

    return OpportunitySnapshot{
        pair,
        *kalshi_top.best_yes_ask,
        *kalshi_top.best_no_ask,
        *polymarket_top.best_yes_ask,
        *polymarket_top.best_no_ask,
        *kalshi_top.best_yes_ask + *polymarket_top.best_no_ask,
        *kalshi_top.best_no_ask + *polymarket_top.best_yes_ask,
        "",
        "",
        "",
        "",
    };
}

std::vector<DetectedOpportunity> detect_opportunities(
    const MarketPair& pair,
    const icarus::core::OrderBook& kalshi_order_book,
    const icarus::core::OrderBook& polymarket_order_book
) {
    std::vector<DetectedOpportunity> opportunities;

    const std::optional<OpportunitySnapshot> snapshot =
        build_opportunity_snapshot(pair, kalshi_order_book, polymarket_order_book);

    if (snapshot.has_value()) {
        const double price_leg_1 = snapshot->kalshi_yes;
        const double price_leg_2 = snapshot->polymarket_no;
        if (price_leg_1 + price_leg_2 < 1.0) {
            opportunities.push_back({
                {pair, 1.0 - (price_leg_1 + price_leg_2), price_leg_1, price_leg_2},
                OpportunityDirection::KalshiYesPolymarketNo,
            });
        }
    }

    if (snapshot.has_value()) {
        const double price_leg_1 = snapshot->kalshi_no;
        const double price_leg_2 = snapshot->polymarket_yes;
        if (price_leg_1 + price_leg_2 < 1.0) {
            opportunities.push_back({
                {pair, 1.0 - (price_leg_1 + price_leg_2), price_leg_1, price_leg_2},
                OpportunityDirection::KalshiNoPolymarketYes,
            });
        }
    }

    return opportunities;
}

}  // namespace icarus::strategy
