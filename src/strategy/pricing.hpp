#pragma once

#include <optional>

#include "core/orderbook.hpp"

namespace icarus::strategy {

struct TopOfBook {
    std::optional<double> best_yes_bid;
    std::optional<double> best_yes_ask;
    std::optional<double> best_no_bid;
    std::optional<double> best_no_ask;
};

TopOfBook extract_top_of_book(const icarus::core::OrderBook& order_book);

}  // namespace icarus::strategy
