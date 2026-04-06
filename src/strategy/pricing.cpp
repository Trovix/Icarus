#include "strategy/pricing.hpp"

namespace icarus::strategy {

namespace {

std::optional<double> first_price(const icarus::core::OrderBookSide& side) {
    if (side.levels.empty()) {
        return std::nullopt;
    }

    return side.levels.front().price;
}

}  // namespace

TopOfBook extract_top_of_book(const icarus::core::OrderBook& order_book) {
    return {
        first_price(order_book.yes_bids),
        first_price(order_book.yes_asks),
        first_price(order_book.no_bids),
        first_price(order_book.no_asks),
    };
}

}  // namespace icarus::strategy
