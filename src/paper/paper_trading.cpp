#include "paper/paper_trading.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <tuple>

namespace icarus::paper {
namespace {

constexpr double kEpsilon = 1e-9;

bool finiteNonNegative(double value) {
    return std::isfinite(value) && value >= 0.0;
}

std::string venueName(core::Venue venue) {
    return venue == core::Venue::Kalshi ? "kalshi" : "polymarket";
}

core::Venue venueFromName(const std::string& value) {
    if (value == "kalshi") return core::Venue::Kalshi;
    if (value == "polymarket") return core::Venue::Polymarket;
    throw std::invalid_argument("unknown venue: " + value);
}

std::string outcomeName(Outcome outcome) {
    return outcome == Outcome::Yes ? "yes" : "no";
}

std::string sideName(OrderSide side) {
    return side == OrderSide::Buy ? "buy" : "sell";
}

OrderSide sideFromName(const std::string& value) {
    if (value == "buy") return OrderSide::Buy;
    if (value == "sell") return OrderSide::Sell;
    throw std::invalid_argument("unknown order side: " + value);
}

std::string lifecycleStatusName(LifecycleStatus status) {
    switch (status) {
        case LifecycleStatus::Open: return "open";
        case LifecycleStatus::ClosingOrphan: return "closing_orphan";
        case LifecycleStatus::Closed: return "closed";
        case LifecycleStatus::Settled: return "settled";
    }
    throw std::logic_error("invalid lifecycle status");
}

LifecycleStatus lifecycleStatusFromName(const std::string& value) {
    if (value == "open") return LifecycleStatus::Open;
    if (value == "closing_orphan") return LifecycleStatus::ClosingOrphan;
    if (value == "closed") return LifecycleStatus::Closed;
    if (value == "settled") return LifecycleStatus::Settled;
    throw std::invalid_argument("unknown lifecycle status: " + value);
}

Outcome outcomeFromName(const std::string& value) {
    if (value == "yes") return Outcome::Yes;
    if (value == "no") return Outcome::No;
    throw std::invalid_argument("unknown outcome: " + value);
}

std::string orderStatusName(OrderStatus status) {
    switch (status) {
        case OrderStatus::Filled: return "filled";
        case OrderStatus::PartiallyFilled: return "partially_filled";
        case OrderStatus::Rejected: return "rejected";
    }
    throw std::logic_error("invalid order status");
}

OrderStatus orderStatusFromName(const std::string& value) {
    if (value == "filled") return OrderStatus::Filled;
    if (value == "partially_filled") return OrderStatus::PartiallyFilled;
    if (value == "rejected") return OrderStatus::Rejected;
    throw std::invalid_argument("unknown order status: " + value);
}

std::string tradeStatusName(TradeStatus status) {
    switch (status) {
        case TradeStatus::Filled: return "filled";
        case TradeStatus::PartiallyFilled: return "partially_filled";
        case TradeStatus::Orphaned: return "orphaned";
        case TradeStatus::Rejected: return "rejected";
    }
    throw std::logic_error("invalid trade status");
}

TradeStatus tradeStatusFromName(const std::string& value) {
    if (value == "filled") return TradeStatus::Filled;
    if (value == "partially_filled") return TradeStatus::PartiallyFilled;
    if (value == "orphaned") return TradeStatus::Orphaned;
    if (value == "rejected") return TradeStatus::Rejected;
    throw std::invalid_argument("unknown trade status: " + value);
}

std::string directionName(strategy::OpportunityDirection direction) {
    return direction == strategy::OpportunityDirection::KalshiYesPolymarketNo
        ? "kalshi_yes_polymarket_no"
        : "kalshi_no_polymarket_yes";
}

strategy::OpportunityDirection directionFromName(const std::string& value) {
    if (value == "kalshi_yes_polymarket_no") {
        return strategy::OpportunityDirection::KalshiYesPolymarketNo;
    }
    if (value == "kalshi_no_polymarket_yes") {
        return strategy::OpportunityDirection::KalshiNoPolymarketYes;
    }
    throw std::invalid_argument("unknown opportunity direction: " + value);
}

const core::OrderBookSide& asks(const core::OrderBook& book, Outcome outcome) {
    return outcome == Outcome::Yes ? book.yes_asks : book.no_asks;
}

const core::OrderBookSide& bids(const core::OrderBook& book, Outcome outcome) {
    return outcome == Outcome::Yes ? book.yes_bids : book.no_bids;
}

double debitForQuantity(const BookWalk& walk, double quantity, const VenueConfig& config) {
    double remaining = std::max(0.0, quantity);
    double debit = 0.0;
    for (const auto& level : walk.levels) {
        const double used = std::min(remaining, level.quantity);
        debit += used * level.execution_price * (1.0 + config.proportional_fee_rate);
        debit += used * config.fee_per_contract;
        remaining -= used;
        if (remaining <= kEpsilon) break;
    }
    return debit;
}

nlohmann::json venueConfigJson(const VenueConfig& value) {
    return {
        {"proportional_fee_rate", value.proportional_fee_rate},
        {"fee_per_contract", value.fee_per_contract},
        {"slippage_buffer_per_contract", value.slippage_buffer_per_contract},
        {"simulated_latency_ms", value.simulated_latency_ms}
    };
}

VenueConfig venueConfigFromJson(const nlohmann::json& value) {
    return {
        value.at("proportional_fee_rate").get<double>(),
        value.at("fee_per_contract").get<double>(),
        value.at("slippage_buffer_per_contract").get<double>(),
        value.at("simulated_latency_ms").get<std::int64_t>()
    };
}

nlohmann::json riskJson(const RiskLimits& value) {
    return {
        {"minimum_net_edge", value.minimum_net_edge},
        {"maximum_trade_quantity", value.maximum_trade_quantity},
        {"maximum_order_notional", value.maximum_order_notional},
        {"maximum_total_exposure", value.maximum_total_exposure},
        {"maximum_venue_exposure", value.maximum_venue_exposure},
        {"maximum_pair_quantity", value.maximum_pair_quantity},
        {"maximum_orphan_quantity", value.maximum_orphan_quantity},
        {"maximum_open_positions", value.maximum_open_positions},
        {"maximum_quote_age_ms", value.maximum_quote_age_ms},
        {"cooldown_ms", value.cooldown_ms}
    };
}

RiskLimits riskFromJson(const nlohmann::json& value) {
    RiskLimits result;
    result.minimum_net_edge = value.at("minimum_net_edge").get<double>();
    result.maximum_trade_quantity = value.at("maximum_trade_quantity").get<double>();
    result.maximum_order_notional = value.at("maximum_order_notional").get<double>();
    result.maximum_total_exposure = value.at("maximum_total_exposure").get<double>();
    result.maximum_venue_exposure = value.at("maximum_venue_exposure").get<double>();
    result.maximum_pair_quantity = value.at("maximum_pair_quantity").get<double>();
    result.maximum_orphan_quantity = value.at("maximum_orphan_quantity").get<double>();
    result.maximum_open_positions = value.at("maximum_open_positions").get<std::size_t>();
    result.maximum_quote_age_ms = value.at("maximum_quote_age_ms").get<std::int64_t>();
    result.cooldown_ms = value.at("cooldown_ms").get<std::int64_t>();
    return result;
}

template <typename T>
void requireFiniteNonNegative(const T& value, const char* name) {
    if (!finiteNonNegative(static_cast<double>(value))) {
        throw std::invalid_argument(std::string(name) + " must be finite and non-negative");
    }
}

}  // namespace

BookWalk walkBuyBook(
    const core::OrderBook& book,
    Outcome outcome,
    double requested_quantity,
    double limit_price,
    double maximum_debit,
    const VenueConfig& config
) {
    BookWalk result;
    result.requested_quantity = requested_quantity;
    if (!finiteNonNegative(requested_quantity) || requested_quantity <= kEpsilon ||
        !finiteNonNegative(limit_price) || !finiteNonNegative(maximum_debit) ||
        !finiteNonNegative(config.proportional_fee_rate) ||
        !finiteNonNegative(config.fee_per_contract) ||
        !finiteNonNegative(config.slippage_buffer_per_contract)) {
        return result;
    }

    double remaining = requested_quantity;
    double remaining_debit = maximum_debit;
    for (const auto& quoted : asks(book, outcome).levels) {
        if (!std::isfinite(quoted.price) || quoted.price < 0.0 || quoted.price > 1.0 ||
            !std::isfinite(quoted.size) || quoted.size <= 0.0) {
            continue;
        }
        const double execution_price = std::min(
            1.0, quoted.price + config.slippage_buffer_per_contract);
        if (execution_price > limit_price + kEpsilon) break;

        const double debit_per_contract =
            execution_price * (1.0 + config.proportional_fee_rate) +
            config.fee_per_contract;
        double quantity = std::min(remaining, quoted.size);
        if (debit_per_contract > kEpsilon) {
            quantity = std::min(quantity, remaining_debit / debit_per_contract);
        }
        if (quantity <= kEpsilon) break;

        result.levels.push_back({quoted.price, execution_price, quantity});
        result.filled_quantity += quantity;
        result.gross_notional += execution_price * quantity;
        const double level_fee = execution_price * quantity * config.proportional_fee_rate +
            quantity * config.fee_per_contract;
        result.fee += level_fee;
        remaining -= quantity;
        remaining_debit -= execution_price * quantity + level_fee;
        if (remaining <= kEpsilon || remaining_debit <= kEpsilon) break;
    }
    if (result.filled_quantity > kEpsilon) {
        result.average_execution_price = result.gross_notional / result.filled_quantity;
    }
    return result;
}

BookWalk walkSellBook(
    const core::OrderBook& book,
    Outcome outcome,
    double requested_quantity,
    double minimum_price,
    const VenueConfig& config
) {
    BookWalk result;
    result.requested_quantity = requested_quantity;
    if (!finiteNonNegative(requested_quantity) || requested_quantity <= kEpsilon ||
        !finiteNonNegative(minimum_price) ||
        !finiteNonNegative(config.proportional_fee_rate) ||
        !finiteNonNegative(config.fee_per_contract) ||
        !finiteNonNegative(config.slippage_buffer_per_contract)) {
        return result;
    }
    double remaining = requested_quantity;
    for (const auto& quoted : bids(book, outcome).levels) {
        if (!std::isfinite(quoted.price) || quoted.price < 0.0 || quoted.price > 1.0 ||
            !std::isfinite(quoted.size) || quoted.size <= 0.0) {
            continue;
        }
        const double execution_price = std::max(
            0.0, quoted.price - config.slippage_buffer_per_contract);
        if (execution_price + kEpsilon < minimum_price) break;
        const double quantity = std::min(remaining, quoted.size);
        if (quantity <= kEpsilon) break;
        result.levels.push_back({quoted.price, execution_price, quantity});
        result.filled_quantity += quantity;
        result.gross_notional += execution_price * quantity;
        result.fee += execution_price * quantity * config.proportional_fee_rate +
            quantity * config.fee_per_contract;
        remaining -= quantity;
        if (remaining <= kEpsilon) break;
    }
    if (result.filled_quantity > kEpsilon) {
        result.average_execution_price = result.gross_notional / result.filled_quantity;
    }
    return result;
}

std::string marketPairKey(const strategy::MarketPair& pair) {
    const auto& kalshi = pair.kalshi.venue_market_id;
    const auto& poly = pair.polymarket.venue_market_id;
    return std::to_string(kalshi.size()) + ":" + kalshi + "|" +
        std::to_string(poly.size()) + ":" + poly;
}

PaperTradingEngine::PaperTradingEngine(
    PaperTradingConfig config,
    std::map<core::Venue, double> starting_cash
) : config_(std::move(config)), initial_cash_(std::move(starting_cash)), cash_(initial_cash_) {
    const auto validateVenue = [](const VenueConfig& venue) {
        requireFiniteNonNegative(venue.proportional_fee_rate, "proportional fee");
        requireFiniteNonNegative(venue.fee_per_contract, "per-contract fee");
        requireFiniteNonNegative(venue.slippage_buffer_per_contract, "slippage buffer");
        if (venue.simulated_latency_ms < 0) {
            throw std::invalid_argument("simulated latency must be non-negative");
        }
    };
    validateVenue(config_.kalshi);
    validateVenue(config_.polymarket);
    const auto& risk = config_.risk;
    requireFiniteNonNegative(risk.minimum_net_edge, "minimum net edge");
    requireFiniteNonNegative(risk.maximum_trade_quantity, "maximum trade quantity");
    requireFiniteNonNegative(risk.maximum_order_notional, "maximum order notional");
    requireFiniteNonNegative(risk.maximum_total_exposure, "maximum total exposure");
    requireFiniteNonNegative(risk.maximum_venue_exposure, "maximum venue exposure");
    requireFiniteNonNegative(risk.maximum_pair_quantity, "maximum pair quantity");
    requireFiniteNonNegative(risk.maximum_orphan_quantity, "maximum orphan quantity");
    if (risk.maximum_quote_age_ms < 0 || risk.cooldown_ms < 0) {
        throw std::invalid_argument("time-based risk limits must be non-negative");
    }
    for (const auto& entry : initial_cash_) {
        requireFiniteNonNegative(entry.second, "starting cash");
    }
    initial_cash_.try_emplace(core::Venue::Kalshi, 0.0);
    initial_cash_.try_emplace(core::Venue::Polymarket, 0.0);
    cash_.try_emplace(core::Venue::Kalshi, 0.0);
    cash_.try_emplace(core::Venue::Polymarket, 0.0);
}

const VenueConfig& PaperTradingEngine::venueConfig(core::Venue venue) const {
    return venue == core::Venue::Kalshi ? config_.kalshi : config_.polymarket;
}

double PaperTradingEngine::totalExposure() const {
    double result = 0.0;
    for (const auto& position : positions_) result += position.cost_basis;
    return result;
}

double PaperTradingEngine::venueExposure(core::Venue venue) const {
    double result = 0.0;
    for (const auto& position : positions_) {
        if (position.venue == venue) result += position.cost_basis;
    }
    return result;
}

double PaperTradingEngine::pairQuantity(const std::string& pair_key) const {
    double result = 0.0;
    for (const auto& position : positions_) {
        if (position.pair_key == pair_key) result += position.quantity;
    }
    return result;
}

std::size_t PaperTradingEngine::openPositionCount() const {
    return static_cast<std::size_t>(std::count_if(
        positions_.begin(), positions_.end(),
        [](const Position& position) { return position.quantity > kEpsilon; }));
}

void PaperTradingEngine::applyOrderToPosition(const OrderRecord& order) {
    if (order.filled_quantity <= kEpsilon) return;
    const auto found = std::find_if(positions_.begin(), positions_.end(), [&](const Position& p) {
        return p.pair_key == order.pair_key && p.venue == order.venue &&
            p.market_id == order.market_id && p.outcome == order.outcome;
    });
    if (found == positions_.end()) {
        positions_.push_back({order.pair_key, order.venue, order.market_id, order.outcome,
                              order.filled_quantity, order.gross_notional + order.fee});
    } else {
        found->quantity += order.filled_quantity;
        found->cost_basis += order.gross_notional + order.fee;
    }
}

ExecutionResult PaperTradingEngine::execute(const TwoLegRequest& request) {
    return executeImpl(request, true);
}

ExecutionResult PaperTradingEngine::executeImpl(
    const TwoLegRequest& request,
    bool require_locked_in_edge
) {
    const auto already_seen = std::find(
        seen_request_ids_.begin(), seen_request_ids_.end(), request.request_id);
    if (request.request_id.empty()) return {false, "request_id is required", std::nullopt};
    if (already_seen != seen_request_ids_.end()) {
        return {false, "duplicate request_id", std::nullopt};
    }
    if (!std::isfinite(request.quantity) || request.quantity <= kEpsilon) {
        return {false, "quantity must be positive and finite", std::nullopt};
    }
    if (request.quantity > config_.risk.maximum_trade_quantity + kEpsilon) {
        return {false, "maximum trade quantity exceeded", std::nullopt};
    }
    if (!std::isfinite(request.maximum_price_per_leg) ||
        request.maximum_price_per_leg < 0.0 || request.maximum_price_per_leg > 1.0) {
        return {false, "maximum leg price must be in [0, 1]", std::nullopt};
    }
    if (request.pair.kalshi.venue != core::Venue::Kalshi ||
        request.pair.polymarket.venue != core::Venue::Polymarket ||
        request.kalshi_book.venue != core::Venue::Kalshi ||
        request.polymarket_book.venue != core::Venue::Polymarket ||
        request.kalshi_book.venue_market_id != request.pair.kalshi.venue_market_id ||
        request.polymarket_book.venue_market_id != request.pair.polymarket.venue_market_id) {
        return {false, "books do not match the requested market pair", std::nullopt};
    }
    const auto quoteIsInvalid = [&](const core::OrderBook& book) {
        const auto age = request.signal_time_unix_ms - book.snapshot_time_unix_ms;
        return age < 0 || age > config_.risk.maximum_quote_age_ms;
    };
    if (quoteIsInvalid(request.kalshi_book) || quoteIsInvalid(request.polymarket_book)) {
        return {false, "stale or future-dated quote", std::nullopt};
    }

    const std::string pair_key = marketPairKey(request.pair);
    const auto last = last_execution_by_pair_.find(pair_key);
    if (last != last_execution_by_pair_.end() &&
        request.signal_time_unix_ms - last->second < config_.risk.cooldown_ms) {
        return {false, "pair is in cooldown", std::nullopt};
    }

    const Outcome kalshi_outcome =
        request.direction == strategy::OpportunityDirection::KalshiYesPolymarketNo
        ? Outcome::Yes : Outcome::No;
    const Outcome polymarket_outcome = request.pair.outcomes_aligned
        ? (kalshi_outcome == Outcome::Yes ? Outcome::No : Outcome::Yes)
        : kalshi_outcome;
    const auto kalshi_preview = walkBuyBook(
        request.kalshi_book, kalshi_outcome, request.quantity,
        request.maximum_price_per_leg, std::numeric_limits<double>::max(), config_.kalshi);
    const auto poly_preview = walkBuyBook(
        request.polymarket_book, polymarket_outcome, request.quantity,
        request.maximum_price_per_leg, std::numeric_limits<double>::max(), config_.polymarket);
    const double potential_pair_quantity = std::min(
        kalshi_preview.filled_quantity, poly_preview.filled_quantity);
    if (potential_pair_quantity <= kEpsilon) {
        return {false, "no executable liquidity for both legs", std::nullopt};
    }
    const double preview_debit =
        debitForQuantity(kalshi_preview, potential_pair_quantity, config_.kalshi) +
        debitForQuantity(poly_preview, potential_pair_quantity, config_.polymarket);
    const double preview_edge = 1.0 - preview_debit / potential_pair_quantity;
    if (require_locked_in_edge && preview_edge + kEpsilon < config_.risk.minimum_net_edge) {
        return {false, "net edge is below the configured minimum", std::nullopt};
    }

    const auto hasPosition = [&](core::Venue venue, const std::string& market_id, Outcome outcome) {
        return std::any_of(positions_.begin(), positions_.end(), [&](const Position& p) {
            return p.quantity > kEpsilon && p.pair_key == pair_key && p.venue == venue &&
                p.market_id == market_id && p.outcome == outcome;
        });
    };
    const std::size_t positions_needed =
        (hasPosition(core::Venue::Kalshi, request.pair.kalshi.venue_market_id,
                     kalshi_outcome) ? 0U : 1U) +
        (hasPosition(core::Venue::Polymarket, request.pair.polymarket.venue_market_id,
                     polymarket_outcome) ? 0U : 1U);
    if (openPositionCount() + positions_needed > config_.risk.maximum_open_positions) {
        return {false, "maximum open positions would be exceeded", std::nullopt};
    }

    // Determine risk-limited capacities before submitting either independent
    // leg. This reserves enough room to keep any resulting orphan within its
    // configured bound without making the fills atomic.
    const double total_room_before = std::max(
        0.0, config_.risk.maximum_total_exposure - totalExposure());
    const double pair_room_before = std::max(
        0.0, config_.risk.maximum_pair_quantity - pairQuantity(pair_key));
    const auto capacity = [&](const core::OrderBook& book, core::Venue venue,
                              Outcome outcome, const VenueConfig& venue_config) {
        const double debit_room = std::min({
            cash_[venue], config_.risk.maximum_order_notional,
            std::max(0.0, config_.risk.maximum_venue_exposure - venueExposure(venue)),
            total_room_before
        });
        return walkBuyBook(
            book, outcome, std::min(request.quantity, pair_room_before),
            request.maximum_price_per_leg, debit_room, venue_config);
    };
    const auto kalshi_capacity = capacity(
        request.kalshi_book, core::Venue::Kalshi, kalshi_outcome, config_.kalshi);
    const auto poly_capacity = capacity(
        request.polymarket_book, core::Venue::Polymarket, polymarket_outcome,
        config_.polymarket);
    double kalshi_quantity_cap = std::min(
        kalshi_capacity.filled_quantity,
        poly_capacity.filled_quantity + config_.risk.maximum_orphan_quantity);
    double poly_quantity_cap = std::min(
        poly_capacity.filled_quantity,
        kalshi_capacity.filled_quantity + config_.risk.maximum_orphan_quantity);
    const double planned_pair_quantity = kalshi_quantity_cap + poly_quantity_cap;
    if (planned_pair_quantity > pair_room_before + kEpsilon &&
        planned_pair_quantity > kEpsilon) {
        const double scale = pair_room_before / planned_pair_quantity;
        kalshi_quantity_cap *= scale;
        poly_quantity_cap *= scale;
    }
    const double planned_debit =
        debitForQuantity(kalshi_capacity, kalshi_quantity_cap, config_.kalshi) +
        debitForQuantity(poly_capacity, poly_quantity_cap, config_.polymarket);
    if (planned_debit > total_room_before + kEpsilon && planned_debit > kEpsilon) {
        const double scale = total_room_before / planned_debit;
        kalshi_quantity_cap *= scale;
        poly_quantity_cap *= scale;
    }

    seen_request_ids_.push_back(request.request_id);
    TradeRecord trade;
    trade.trade_id = "trade-" + std::to_string(next_trade_id_++);
    trade.request_id = request.request_id;
    trade.pair_key = pair_key;
    trade.direction = request.direction;
    trade.requested_quantity = request.quantity;
    trade.signal_time_unix_ms = request.signal_time_unix_ms;

    auto executeLeg = [&](core::Venue venue, const std::string& market_id, Outcome outcome,
                          const core::OrderBook& book, double quantity_cap) {
        OrderRecord order;
        order.order_id = "order-" + std::to_string(next_order_id_++);
        order.trade_id = trade.trade_id;
        order.pair_key = pair_key;
        order.venue = venue;
        order.market_id = market_id;
        order.outcome = outcome;
        order.requested_quantity = request.quantity;
        order.limit_price = request.maximum_price_per_leg;
        order.submitted_time_unix_ms = request.signal_time_unix_ms;
        order.execution_time_unix_ms = request.signal_time_unix_ms +
            venueConfig(venue).simulated_latency_ms;
        order.source_snapshot_time_unix_ms = book.snapshot_time_unix_ms;

        const bool is_new_position = std::none_of(
            positions_.begin(), positions_.end(), [&](const Position& p) {
                return p.quantity > kEpsilon && p.pair_key == pair_key && p.venue == venue &&
                    p.market_id == market_id && p.outcome == outcome;
            });
        if (is_new_position && openPositionCount() >= config_.risk.maximum_open_positions) {
            order.status = OrderStatus::Rejected;
            order.rejection_reason = "maximum open positions reached";
            orders_.push_back(order);
            trade.order_ids.push_back(order.order_id);
            return order;
        }

        const double total_room = std::max(0.0, config_.risk.maximum_total_exposure - totalExposure());
        const double venue_room = std::max(
            0.0, config_.risk.maximum_venue_exposure - venueExposure(venue));
        const double pair_room = std::max(
            0.0, config_.risk.maximum_pair_quantity - pairQuantity(pair_key));
        const double maximum_debit = std::min({
            cash_[venue], config_.risk.maximum_order_notional, total_room, venue_room
        });
        const double risk_quantity = std::min({
            request.quantity, pair_room, quantity_cap
        });
        const auto walked = walkBuyBook(
            book, outcome, risk_quantity, request.maximum_price_per_leg,
            maximum_debit, venueConfig(venue));
        order.filled_quantity = walked.filled_quantity;
        order.average_execution_price = walked.average_execution_price;
        order.gross_notional = walked.gross_notional;
        order.fee = walked.fee;
        if (walked.filled_quantity <= kEpsilon) {
            order.status = OrderStatus::Rejected;
            order.rejection_reason = "risk limits, cash, or liquidity prevented a fill";
        } else if (walked.filled_quantity + kEpsilon < request.quantity) {
            order.status = OrderStatus::PartiallyFilled;
        } else {
            order.status = OrderStatus::Filled;
        }

        for (const auto& level : walked.levels) {
            FillRecord fill;
            fill.fill_id = "fill-" + std::to_string(next_fill_id_++);
            fill.order_id = order.order_id;
            fill.venue = venue;
            fill.market_id = market_id;
            fill.outcome = outcome;
            fill.quantity = level.quantity;
            fill.book_price = level.book_price;
            fill.execution_price = level.execution_price;
            fill.gross_notional = level.quantity * level.execution_price;
            fill.fee = fill.gross_notional * venueConfig(venue).proportional_fee_rate +
                level.quantity * venueConfig(venue).fee_per_contract;
            fill.execution_time_unix_ms = order.execution_time_unix_ms;
            fills_.push_back(std::move(fill));
        }
        cash_[venue] -= order.gross_notional + order.fee;
        if (std::abs(cash_[venue]) < kEpsilon) cash_[venue] = 0.0;
        applyOrderToPosition(order);
        orders_.push_back(order);
        trade.order_ids.push_back(order.order_id);
        return order;
    };

    const auto kalshi_order = executeLeg(
        core::Venue::Kalshi, request.pair.kalshi.venue_market_id, kalshi_outcome,
        request.kalshi_book, kalshi_quantity_cap);
    const auto polymarket_order = executeLeg(
        core::Venue::Polymarket, request.pair.polymarket.venue_market_id, polymarket_outcome,
        request.polymarket_book, poly_quantity_cap);

    trade.paired_quantity = std::min(
        kalshi_order.filled_quantity, polymarket_order.filled_quantity);
    if (trade.paired_quantity > kEpsilon) {
        const double paired_cost = trade.paired_quantity * (
            (kalshi_order.gross_notional + kalshi_order.fee) /
                kalshi_order.filled_quantity +
            (polymarket_order.gross_notional + polymarket_order.fee) /
                polymarket_order.filled_quantity);
        trade.estimated_locked_in_pnl = trade.paired_quantity - paired_cost;
    }
    const double imbalance = kalshi_order.filled_quantity - polymarket_order.filled_quantity;
    if (std::abs(imbalance) > kEpsilon) {
        const auto& orphan_order = imbalance > 0.0 ? kalshi_order : polymarket_order;
        const double orphan_quantity = std::abs(imbalance);
        const double unit_cost = orphan_order.filled_quantity > kEpsilon
            ? (orphan_order.gross_notional + orphan_order.fee) / orphan_order.filled_quantity
            : 0.0;
        trade.orphan = OrphanExposure{
            orphan_order.venue, orphan_order.market_id, orphan_order.outcome,
            orphan_quantity, orphan_quantity * unit_cost};
        trade.status = TradeStatus::Orphaned;
    } else if (trade.paired_quantity <= kEpsilon) {
        trade.status = TradeStatus::Rejected;
        trade.rejection_reason = "neither leg filled";
    } else if (trade.paired_quantity + kEpsilon < request.quantity) {
        trade.status = TradeStatus::PartiallyFilled;
    } else {
        trade.status = TradeStatus::Filled;
    }
    if (kalshi_order.filled_quantity > kEpsilon || polymarket_order.filled_quantity > kEpsilon) {
        last_execution_by_pair_[pair_key] = request.signal_time_unix_ms;
    }
    trades_.push_back(trade);
    return {trade.status != TradeStatus::Rejected, trade.rejection_reason, trade};
}

ExecutionResult PaperTradingEngine::openConvergence(const OpenConvergenceRequest& request) {
    const auto& policy = request.exit_policy;
    if (!std::isfinite(policy.minimum_combined_exit_bid) ||
        policy.minimum_combined_exit_bid < 0.0 || policy.minimum_combined_exit_bid > 2.0 ||
        !finiteNonNegative(policy.profit_target) || !finiteNonNegative(policy.stop_loss) ||
        policy.maximum_hold_ms < 0) {
        return {false, "invalid convergence exit policy", std::nullopt};
    }
    if (!finiteNonNegative(request.absolute_entry_spread) ||
        !finiteNonNegative(request.minimum_entry_spread) ||
        request.absolute_entry_spread > 1.0 || request.minimum_entry_spread > 1.0 ||
        request.absolute_entry_spread + kEpsilon < request.minimum_entry_spread) {
        return {false, "entry divergence is below the configured convergence minimum",
                std::nullopt};
    }
    const std::string requested_pair_key = marketPairKey(request.entry.pair);
    const bool already_open = std::any_of(
        open_trades_.begin(), open_trades_.end(), [&](const OpenConvergenceTrade& lifecycle) {
            return lifecycle.pair_key == requested_pair_key &&
                lifecycle.direction == request.entry.direction &&
                (lifecycle.status == LifecycleStatus::Open ||
                 lifecycle.status == LifecycleStatus::ClosingOrphan);
        });
    if (already_open) {
        return {false, "pair and direction already have an open convergence lifecycle",
                std::nullopt};
    }
    auto result = executeImpl(request.entry, false);
    if (!result.accepted || !result.trade) return result;

    const auto findOrder = [&](core::Venue venue) -> const OrderRecord* {
        for (const auto& id : result.trade->order_ids) {
            const auto found = std::find_if(orders_.begin(), orders_.end(), [&](const OrderRecord& o) {
                return o.order_id == id && o.venue == venue;
            });
            if (found != orders_.end()) return &*found;
        }
        return nullptr;
    };
    const auto* kalshi_order = findOrder(core::Venue::Kalshi);
    const auto* poly_order = findOrder(core::Venue::Polymarket);
    if (!kalshi_order || !poly_order) {
        throw std::logic_error("accepted entry is missing an order record");
    }
    OpenConvergenceTrade lifecycle;
    lifecycle.lifecycle_id = "lifecycle-" + std::to_string(next_lifecycle_id_++);
    lifecycle.entry_trade_id = result.trade->trade_id;
    lifecycle.pair_key = result.trade->pair_key;
    lifecycle.direction = result.trade->direction;
    lifecycle.kalshi_market_id = kalshi_order->market_id;
    lifecycle.polymarket_market_id = poly_order->market_id;
    lifecycle.kalshi_outcome = kalshi_order->outcome;
    lifecycle.polymarket_outcome = poly_order->outcome;
    lifecycle.kalshi_open_quantity = kalshi_order->filled_quantity;
    lifecycle.polymarket_open_quantity = poly_order->filled_quantity;
    lifecycle.kalshi_cost_basis = kalshi_order->gross_notional + kalshi_order->fee;
    lifecycle.polymarket_cost_basis = poly_order->gross_notional + poly_order->fee;
    lifecycle.absolute_entry_spread = request.absolute_entry_spread;
    lifecycle.opened_time_unix_ms = request.entry.signal_time_unix_ms;
    lifecycle.exit_policy = policy;
    lifecycle.status = result.trade->orphan
        ? LifecycleStatus::ClosingOrphan : LifecycleStatus::Open;
    open_trades_.push_back(std::move(lifecycle));
    return result;
}

ConvergenceCloseResult PaperTradingEngine::evaluateAndClose(
    const ConvergenceCloseRequest& request
) {
    if (request.request_id.empty()) return {false, false, ExitReason::None, 0.0,
                                             std::nullopt, "request_id is required"};
    if (std::find(seen_request_ids_.begin(), seen_request_ids_.end(), request.request_id) !=
        seen_request_ids_.end()) {
        return {false, false, ExitReason::None, 0.0, std::nullopt, "duplicate request_id"};
    }
    auto lifecycle_it = std::find_if(
        open_trades_.begin(), open_trades_.end(), [&](const OpenConvergenceTrade& value) {
            return value.lifecycle_id == request.lifecycle_id;
        });
    if (lifecycle_it == open_trades_.end()) {
        return {false, false, ExitReason::None, 0.0, std::nullopt, "unknown lifecycle_id"};
    }
    auto& lifecycle = *lifecycle_it;
    if (lifecycle.status == LifecycleStatus::Closed ||
        lifecycle.status == LifecycleStatus::Settled) {
        return {false, true, ExitReason::None, 0.0, std::nullopt, "lifecycle is already closed"};
    }
    if (!std::isfinite(request.minimum_exit_price_per_leg) ||
        request.minimum_exit_price_per_leg < 0.0 || request.minimum_exit_price_per_leg > 1.0) {
        return {false, false, ExitReason::None, 0.0, std::nullopt,
                "minimum exit price must be in [0, 1]"};
    }
    if (request.kalshi_book.venue != core::Venue::Kalshi ||
        request.polymarket_book.venue != core::Venue::Polymarket ||
        request.kalshi_book.venue_market_id != lifecycle.kalshi_market_id ||
        request.polymarket_book.venue_market_id != lifecycle.polymarket_market_id) {
        return {false, false, ExitReason::None, 0.0, std::nullopt,
                "books do not match the open lifecycle"};
    }
    const auto quoteIsInvalid = [&](const core::OrderBook& book) {
        const auto age = request.evaluation_time_unix_ms - book.snapshot_time_unix_ms;
        return age < 0 || age > config_.risk.maximum_quote_age_ms;
    };
    if (quoteIsInvalid(request.kalshi_book) || quoteIsInvalid(request.polymarket_book)) {
        return {false, false, ExitReason::None, 0.0, std::nullopt,
                "stale or future-dated exit quote"};
    }

    const auto kalshi_preview = walkSellBook(
        request.kalshi_book, lifecycle.kalshi_outcome, lifecycle.kalshi_open_quantity,
        request.minimum_exit_price_per_leg, config_.kalshi);
    const auto poly_preview = walkSellBook(
        request.polymarket_book, lifecycle.polymarket_outcome,
        lifecycle.polymarket_open_quantity, request.minimum_exit_price_per_leg,
        config_.polymarket);
    const auto previewPnl = [](const BookWalk& walk, double open_quantity,
                               double cost_basis) {
        if (walk.filled_quantity <= kEpsilon || open_quantity <= kEpsilon) return 0.0;
        const double released_basis = cost_basis * walk.filled_quantity / open_quantity;
        return walk.gross_notional - walk.fee - released_basis;
    };
    const double executable_pnl = lifecycle.realized_pnl +
        previewPnl(kalshi_preview, lifecycle.kalshi_open_quantity,
                   lifecycle.kalshi_cost_basis) +
        previewPnl(poly_preview, lifecycle.polymarket_open_quantity,
                   lifecycle.polymarket_cost_basis);
    const bool has_paired_exit = kalshi_preview.filled_quantity > kEpsilon &&
        poly_preview.filled_quantity > kEpsilon;
    const double combined_exit_bid = has_paired_exit
        ? kalshi_preview.average_execution_price + poly_preview.average_execution_price
        : 0.0;
    const auto& policy = lifecycle.exit_policy;
    ExitReason reason = ExitReason::None;
    if (request.force) {
        reason = ExitReason::Forced;
    } else if (policy.stop_loss > kEpsilon && executable_pnl <= -policy.stop_loss + kEpsilon) {
        reason = ExitReason::StopLoss;
    } else if (policy.maximum_hold_ms > 0 &&
               request.evaluation_time_unix_ms - lifecycle.opened_time_unix_ms >=
                   policy.maximum_hold_ms) {
        reason = ExitReason::MaximumHold;
    } else if (policy.profit_target > kEpsilon &&
               executable_pnl + kEpsilon >= policy.profit_target) {
        reason = ExitReason::ProfitTarget;
    } else if (has_paired_exit &&
               combined_exit_bid + kEpsilon >= policy.minimum_combined_exit_bid) {
        reason = ExitReason::Converged;
    }
    if (reason == ExitReason::None) {
        return {false, false, reason, 0.0, std::nullopt, "exit conditions not met"};
    }

    seen_request_ids_.push_back(request.request_id);
    double kalshi_cap = kalshi_preview.filled_quantity;
    double poly_cap = poly_preview.filled_quantity;
    // Avoid creating an exit-side imbalance beyond the same orphan limit used
    // at entry. Reducing a close quantity never assumes unavailable liquidity.
    double remaining_k = lifecycle.kalshi_open_quantity - kalshi_cap;
    double remaining_p = lifecycle.polymarket_open_quantity - poly_cap;
    if (remaining_k > remaining_p + config_.risk.maximum_orphan_quantity) {
        poly_cap = std::max(
            0.0, poly_cap - (remaining_k - remaining_p - config_.risk.maximum_orphan_quantity));
    } else if (remaining_p > remaining_k + config_.risk.maximum_orphan_quantity) {
        kalshi_cap = std::max(
            0.0, kalshi_cap - (remaining_p - remaining_k - config_.risk.maximum_orphan_quantity));
    }

    auto closeLeg = [&](core::Venue venue, const core::OrderBook& book,
                        const std::string& market_id, Outcome outcome, double quantity_cap,
                        double& open_quantity, double& lifecycle_cost_basis) {
        const double quantity_before = open_quantity;
        OrderRecord order;
        order.order_id = "order-" + std::to_string(next_order_id_++);
        order.trade_id = lifecycle.lifecycle_id;
        order.pair_key = lifecycle.pair_key;
        order.venue = venue;
        order.market_id = market_id;
        order.outcome = outcome;
        order.side = OrderSide::Sell;
        order.requested_quantity = quantity_before;
        order.limit_price = request.minimum_exit_price_per_leg;
        order.submitted_time_unix_ms = request.evaluation_time_unix_ms;
        order.execution_time_unix_ms = request.evaluation_time_unix_ms +
            venueConfig(venue).simulated_latency_ms;
        order.source_snapshot_time_unix_ms = book.snapshot_time_unix_ms;
        const auto walked = walkSellBook(
            book, outcome, quantity_cap, request.minimum_exit_price_per_leg,
            venueConfig(venue));
        order.filled_quantity = walked.filled_quantity;
        order.average_execution_price = walked.average_execution_price;
        order.gross_notional = walked.gross_notional;
        order.fee = walked.fee;
        if (walked.filled_quantity <= kEpsilon) {
            order.status = OrderStatus::Rejected;
            order.rejection_reason = "no executable bid liquidity";
        } else if (walked.filled_quantity + kEpsilon < quantity_before) {
            order.status = OrderStatus::PartiallyFilled;
        } else {
            order.status = OrderStatus::Filled;
        }
        for (const auto& level : walked.levels) {
            FillRecord fill;
            fill.fill_id = "fill-" + std::to_string(next_fill_id_++);
            fill.order_id = order.order_id;
            fill.venue = venue;
            fill.market_id = market_id;
            fill.outcome = outcome;
            fill.side = OrderSide::Sell;
            fill.quantity = level.quantity;
            fill.book_price = level.book_price;
            fill.execution_price = level.execution_price;
            fill.gross_notional = level.quantity * level.execution_price;
            fill.fee = fill.gross_notional * venueConfig(venue).proportional_fee_rate +
                level.quantity * venueConfig(venue).fee_per_contract;
            fill.execution_time_unix_ms = order.execution_time_unix_ms;
            fills_.push_back(std::move(fill));
        }
        double released_basis = 0.0;
        if (quantity_before > kEpsilon) {
            released_basis = lifecycle_cost_basis * walked.filled_quantity / quantity_before;
        }
        lifecycle_cost_basis -= released_basis;
        open_quantity -= walked.filled_quantity;
        if (open_quantity < kEpsilon) open_quantity = 0.0;
        if (lifecycle_cost_basis < kEpsilon) lifecycle_cost_basis = 0.0;
        const double leg_pnl = walked.gross_notional - walked.fee - released_basis;
        lifecycle.realized_pnl += leg_pnl;
        realized_pnl_ += leg_pnl;
        cash_[venue] += walked.gross_notional - walked.fee;

        const auto position = std::find_if(positions_.begin(), positions_.end(),
            [&](const Position& p) {
                return p.pair_key == lifecycle.pair_key && p.venue == venue &&
                    p.market_id == market_id && p.outcome == outcome;
            });
        if (position == positions_.end() && walked.filled_quantity > kEpsilon) {
            throw std::logic_error("closing fill has no matching position");
        }
        if (position != positions_.end()) {
            position->quantity -= walked.filled_quantity;
            position->cost_basis -= released_basis;
            if (position->quantity < kEpsilon) position->quantity = 0.0;
            if (position->cost_basis < kEpsilon) position->cost_basis = 0.0;
        }
        orders_.push_back(order);
        lifecycle.closing_order_ids.push_back(order.order_id);
        return leg_pnl;
    };

    const double kalshi_pnl = closeLeg(
        core::Venue::Kalshi, request.kalshi_book, lifecycle.kalshi_market_id,
        lifecycle.kalshi_outcome, kalshi_cap, lifecycle.kalshi_open_quantity,
        lifecycle.kalshi_cost_basis);
    const double poly_pnl = closeLeg(
        core::Venue::Polymarket, request.polymarket_book, lifecycle.polymarket_market_id,
        lifecycle.polymarket_outcome, poly_cap, lifecycle.polymarket_open_quantity,
        lifecycle.polymarket_cost_basis);

    ConvergenceCloseResult result;
    result.triggered = true;
    result.reason = reason;
    result.realized_pnl = kalshi_pnl + poly_pnl;
    result.fully_closed = lifecycle.kalshi_open_quantity <= kEpsilon &&
        lifecycle.polymarket_open_quantity <= kEpsilon;
    if (result.fully_closed) {
        lifecycle.status = LifecycleStatus::Closed;
    } else {
        const double imbalance = lifecycle.kalshi_open_quantity -
            lifecycle.polymarket_open_quantity;
        if (std::abs(imbalance) > kEpsilon) {
            const bool kalshi_orphan = imbalance > 0.0;
            const double quantity = std::abs(imbalance);
            const double open_quantity = kalshi_orphan ? lifecycle.kalshi_open_quantity
                                                        : lifecycle.polymarket_open_quantity;
            const double basis = kalshi_orphan ? lifecycle.kalshi_cost_basis
                                                : lifecycle.polymarket_cost_basis;
            result.remaining_orphan = OrphanExposure{
                kalshi_orphan ? core::Venue::Kalshi : core::Venue::Polymarket,
                kalshi_orphan ? lifecycle.kalshi_market_id : lifecycle.polymarket_market_id,
                kalshi_orphan ? lifecycle.kalshi_outcome : lifecycle.polymarket_outcome,
                quantity, open_quantity > kEpsilon ? basis * quantity / open_quantity : 0.0};
            lifecycle.status = LifecycleStatus::ClosingOrphan;
        } else {
            lifecycle.status = LifecycleStatus::Open;
        }
    }
    return result;
}

SettlementRecord PaperTradingEngine::settle(
    const std::string& pair_key,
    Outcome winning_outcome,
    std::int64_t settlement_time_unix_ms
) {
    SettlementRecord record;
    record.settlement_id = "settlement-" + std::to_string(next_settlement_id_++);
    record.pair_key = pair_key;
    record.winning_outcome = winning_outcome;
    record.settlement_time_unix_ms = settlement_time_unix_ms;
    for (auto& lifecycle : open_trades_) {
        if (lifecycle.pair_key != pair_key || lifecycle.status == LifecycleStatus::Closed ||
            lifecycle.status == LifecycleStatus::Settled) {
            continue;
        }
        const double lifecycle_payout =
            (lifecycle.kalshi_outcome == winning_outcome
                ? lifecycle.kalshi_open_quantity : 0.0) +
            (lifecycle.polymarket_outcome == winning_outcome
                ? lifecycle.polymarket_open_quantity : 0.0);
        lifecycle.realized_pnl += lifecycle_payout - lifecycle.kalshi_cost_basis -
            lifecycle.polymarket_cost_basis;
        lifecycle.kalshi_open_quantity = 0.0;
        lifecycle.polymarket_open_quantity = 0.0;
        lifecycle.kalshi_cost_basis = 0.0;
        lifecycle.polymarket_cost_basis = 0.0;
        lifecycle.status = LifecycleStatus::Settled;
    }
    for (auto& position : positions_) {
        if (position.pair_key != pair_key || position.quantity <= kEpsilon) continue;
        const double payout = position.outcome == winning_outcome ? position.quantity : 0.0;
        cash_[position.venue] += payout;
        record.payout += payout;
        record.released_cost_basis += position.cost_basis;
        position.quantity = 0.0;
        position.cost_basis = 0.0;
    }
    if (record.released_cost_basis <= kEpsilon) {
        throw std::invalid_argument("cannot settle a pair without an open position");
    }
    record.realized_pnl = record.payout - record.released_cost_basis;
    realized_pnl_ += record.realized_pnl;
    settlements_.push_back(record);
    return record;
}

PortfolioSummary PaperTradingEngine::portfolio(const std::vector<MarkPrice>& marks) const {
    PortfolioSummary result;
    for (const auto& entry : cash_) result.cash += entry.second;
    result.realized_pnl = realized_pnl_;

    struct PairTotals { double yes_qty = 0.0; double yes_cost = 0.0;
                        double no_qty = 0.0; double no_cost = 0.0; };
    std::map<std::string, PairTotals> pairs;
    double marked_value = 0.0;
    for (const auto& position : positions_) {
        if (position.quantity <= kEpsilon) continue;
        result.open_cost_basis += position.cost_basis;
        auto& totals = pairs[position.pair_key];
        if (position.outcome == Outcome::Yes) {
            totals.yes_qty += position.quantity;
            totals.yes_cost += position.cost_basis;
        } else {
            totals.no_qty += position.quantity;
            totals.no_cost += position.cost_basis;
        }
        const auto mark = std::find_if(marks.begin(), marks.end(), [&](const MarkPrice& value) {
            return value.pair_key == position.pair_key && value.venue == position.venue &&
                value.market_id == position.market_id && value.outcome == position.outcome;
        });
        if (mark != marks.end() && std::isfinite(mark->price) &&
            mark->price >= 0.0 && mark->price <= 1.0) {
            marked_value += position.quantity * mark->price;
        }
    }
    for (const auto& entry : pairs) {
        const auto& value = entry.second;
        const double matched = std::min(value.yes_qty, value.no_qty);
        if (matched <= kEpsilon) continue;
        const double matched_cost = matched * (
            value.yes_cost / value.yes_qty + value.no_cost / value.no_qty);
        result.locked_in_pnl += matched - matched_cost;
    }
    result.unrealized_pnl = marked_value - result.open_cost_basis;
    result.total_equity = result.cash + marked_value;
    return result;
}

double PaperTradingEngine::cash(core::Venue venue) const {
    const auto found = cash_.find(venue);
    return found == cash_.end() ? 0.0 : found->second;
}

nlohmann::json PaperTradingEngine::toJson() const {
    nlohmann::json json;
    json["schema_version"] = 1;
    json["config"] = {
        {"kalshi", venueConfigJson(config_.kalshi)},
        {"polymarket", venueConfigJson(config_.polymarket)},
        {"risk", riskJson(config_.risk)}
    };
    for (const auto& entry : initial_cash_) json["initial_cash"][venueName(entry.first)] = entry.second;
    for (const auto& entry : cash_) json["cash"][venueName(entry.first)] = entry.second;
    json["realized_pnl"] = realized_pnl_;
    json["counters"] = {
        {"order", next_order_id_}, {"fill", next_fill_id_}, {"trade", next_trade_id_},
        {"settlement", next_settlement_id_}, {"lifecycle", next_lifecycle_id_}
    };
    json["seen_request_ids"] = seen_request_ids_;
    json["last_execution_by_pair"] = last_execution_by_pair_;

    json["positions"] = nlohmann::json::array();
    for (const auto& p : positions_) json["positions"].push_back({
        {"pair_key", p.pair_key}, {"venue", venueName(p.venue)}, {"market_id", p.market_id},
        {"outcome", outcomeName(p.outcome)}, {"quantity", p.quantity}, {"cost_basis", p.cost_basis}
    });
    json["fills"] = nlohmann::json::array();
    for (const auto& f : fills_) json["fills"].push_back({
        {"fill_id", f.fill_id}, {"order_id", f.order_id}, {"venue", venueName(f.venue)},
        {"market_id", f.market_id}, {"outcome", outcomeName(f.outcome)},
        {"side", sideName(f.side)}, {"quantity", f.quantity},
        {"book_price", f.book_price}, {"execution_price", f.execution_price},
        {"gross_notional", f.gross_notional}, {"fee", f.fee},
        {"execution_time_unix_ms", f.execution_time_unix_ms}
    });
    json["orders"] = nlohmann::json::array();
    for (const auto& o : orders_) json["orders"].push_back({
        {"order_id", o.order_id}, {"trade_id", o.trade_id}, {"pair_key", o.pair_key},
        {"venue", venueName(o.venue)}, {"market_id", o.market_id},
        {"outcome", outcomeName(o.outcome)}, {"side", sideName(o.side)},
        {"requested_quantity", o.requested_quantity}, {"filled_quantity", o.filled_quantity},
        {"limit_price", o.limit_price}, {"average_execution_price", o.average_execution_price},
        {"gross_notional", o.gross_notional}, {"fee", o.fee},
        {"submitted_time_unix_ms", o.submitted_time_unix_ms},
        {"execution_time_unix_ms", o.execution_time_unix_ms},
        {"source_snapshot_time_unix_ms", o.source_snapshot_time_unix_ms},
        {"status", orderStatusName(o.status)}, {"rejection_reason", o.rejection_reason}
    });
    json["trades"] = nlohmann::json::array();
    for (const auto& t : trades_) {
        nlohmann::json value = {
            {"trade_id", t.trade_id}, {"request_id", t.request_id}, {"pair_key", t.pair_key},
            {"direction", directionName(t.direction)}, {"requested_quantity", t.requested_quantity},
            {"paired_quantity", t.paired_quantity},
            {"estimated_locked_in_pnl", t.estimated_locked_in_pnl},
            {"signal_time_unix_ms", t.signal_time_unix_ms}, {"order_ids", t.order_ids},
            {"status", tradeStatusName(t.status)}, {"rejection_reason", t.rejection_reason}
        };
        if (t.orphan) value["orphan"] = {
            {"venue", venueName(t.orphan->venue)}, {"market_id", t.orphan->market_id},
            {"outcome", outcomeName(t.orphan->outcome)}, {"quantity", t.orphan->quantity},
            {"cost_basis", t.orphan->cost_basis}
        };
        json["trades"].push_back(std::move(value));
    }
    json["settlements"] = nlohmann::json::array();
    for (const auto& s : settlements_) json["settlements"].push_back({
        {"settlement_id", s.settlement_id}, {"pair_key", s.pair_key},
        {"winning_outcome", outcomeName(s.winning_outcome)}, {"payout", s.payout},
        {"released_cost_basis", s.released_cost_basis}, {"realized_pnl", s.realized_pnl},
        {"settlement_time_unix_ms", s.settlement_time_unix_ms}
    });
    json["open_trades"] = nlohmann::json::array();
    for (const auto& t : open_trades_) json["open_trades"].push_back({
        {"lifecycle_id", t.lifecycle_id}, {"entry_trade_id", t.entry_trade_id},
        {"pair_key", t.pair_key}, {"direction", directionName(t.direction)},
        {"kalshi_market_id", t.kalshi_market_id},
        {"polymarket_market_id", t.polymarket_market_id},
        {"kalshi_outcome", outcomeName(t.kalshi_outcome)},
        {"polymarket_outcome", outcomeName(t.polymarket_outcome)},
        {"kalshi_open_quantity", t.kalshi_open_quantity},
        {"polymarket_open_quantity", t.polymarket_open_quantity},
        {"kalshi_cost_basis", t.kalshi_cost_basis},
        {"polymarket_cost_basis", t.polymarket_cost_basis},
        {"realized_pnl", t.realized_pnl},
        {"absolute_entry_spread", t.absolute_entry_spread},
        {"opened_time_unix_ms", t.opened_time_unix_ms},
        {"exit_policy", {
            {"minimum_combined_exit_bid", t.exit_policy.minimum_combined_exit_bid},
            {"profit_target", t.exit_policy.profit_target},
            {"stop_loss", t.exit_policy.stop_loss},
            {"maximum_hold_ms", t.exit_policy.maximum_hold_ms}
        }},
        {"status", lifecycleStatusName(t.status)},
        {"closing_order_ids", t.closing_order_ids}
    });
    return json;
}

PaperTradingEngine PaperTradingEngine::fromJson(const nlohmann::json& json) {
    if (json.at("schema_version").get<int>() != 1) {
        throw std::invalid_argument("unsupported paper-trading snapshot version");
    }
    PaperTradingConfig config;
    config.kalshi = venueConfigFromJson(json.at("config").at("kalshi"));
    config.polymarket = venueConfigFromJson(json.at("config").at("polymarket"));
    config.risk = riskFromJson(json.at("config").at("risk"));
    std::map<core::Venue, double> initial_cash;
    for (const auto& entry : json.at("initial_cash").items()) {
        initial_cash[venueFromName(entry.key())] = entry.value().get<double>();
    }
    PaperTradingEngine engine(config, initial_cash);
    engine.cash_.clear();
    for (const auto& entry : json.at("cash").items()) {
        engine.cash_[venueFromName(entry.key())] = entry.value().get<double>();
    }
    engine.realized_pnl_ = json.at("realized_pnl").get<double>();
    engine.next_order_id_ = json.at("counters").at("order").get<std::uint64_t>();
    engine.next_fill_id_ = json.at("counters").at("fill").get<std::uint64_t>();
    engine.next_trade_id_ = json.at("counters").at("trade").get<std::uint64_t>();
    engine.next_settlement_id_ = json.at("counters").at("settlement").get<std::uint64_t>();
    engine.next_lifecycle_id_ = json.at("counters").value("lifecycle", std::uint64_t{1});
    engine.seen_request_ids_ = json.at("seen_request_ids").get<std::vector<std::string>>();
    engine.last_execution_by_pair_ =
        json.at("last_execution_by_pair").get<std::map<std::string, std::int64_t>>();

    for (const auto& p : json.at("positions")) engine.positions_.push_back({
        p.at("pair_key").get<std::string>(), venueFromName(p.at("venue").get<std::string>()),
        p.at("market_id").get<std::string>(), outcomeFromName(p.at("outcome").get<std::string>()),
        p.at("quantity").get<double>(), p.at("cost_basis").get<double>()
    });
    for (const auto& f : json.at("fills")) {
        FillRecord value;
        value.fill_id = f.at("fill_id").get<std::string>();
        value.order_id = f.at("order_id").get<std::string>();
        value.venue = venueFromName(f.at("venue").get<std::string>());
        value.market_id = f.at("market_id").get<std::string>();
        value.outcome = outcomeFromName(f.at("outcome").get<std::string>());
        value.side = sideFromName(f.value("side", std::string{"buy"}));
        value.quantity = f.at("quantity").get<double>();
        value.book_price = f.at("book_price").get<double>();
        value.execution_price = f.at("execution_price").get<double>();
        value.gross_notional = f.at("gross_notional").get<double>();
        value.fee = f.at("fee").get<double>();
        value.execution_time_unix_ms = f.at("execution_time_unix_ms").get<std::int64_t>();
        engine.fills_.push_back(std::move(value));
    }
    for (const auto& o : json.at("orders")) {
        OrderRecord value;
        value.order_id = o.at("order_id").get<std::string>();
        value.trade_id = o.at("trade_id").get<std::string>();
        value.pair_key = o.at("pair_key").get<std::string>();
        value.venue = venueFromName(o.at("venue").get<std::string>());
        value.market_id = o.at("market_id").get<std::string>();
        value.outcome = outcomeFromName(o.at("outcome").get<std::string>());
        value.side = sideFromName(o.value("side", std::string{"buy"}));
        value.requested_quantity = o.at("requested_quantity").get<double>();
        value.filled_quantity = o.at("filled_quantity").get<double>();
        value.limit_price = o.at("limit_price").get<double>();
        value.average_execution_price = o.at("average_execution_price").get<double>();
        value.gross_notional = o.at("gross_notional").get<double>();
        value.fee = o.at("fee").get<double>();
        value.submitted_time_unix_ms = o.at("submitted_time_unix_ms").get<std::int64_t>();
        value.execution_time_unix_ms = o.at("execution_time_unix_ms").get<std::int64_t>();
        value.source_snapshot_time_unix_ms =
            o.at("source_snapshot_time_unix_ms").get<std::int64_t>();
        value.status = orderStatusFromName(o.at("status").get<std::string>());
        value.rejection_reason = o.at("rejection_reason").get<std::string>();
        engine.orders_.push_back(std::move(value));
    }
    for (const auto& t : json.at("trades")) {
        TradeRecord value;
        value.trade_id = t.at("trade_id").get<std::string>();
        value.request_id = t.at("request_id").get<std::string>();
        value.pair_key = t.at("pair_key").get<std::string>();
        value.direction = directionFromName(t.at("direction").get<std::string>());
        value.requested_quantity = t.at("requested_quantity").get<double>();
        value.paired_quantity = t.at("paired_quantity").get<double>();
        value.estimated_locked_in_pnl = t.at("estimated_locked_in_pnl").get<double>();
        value.signal_time_unix_ms = t.at("signal_time_unix_ms").get<std::int64_t>();
        value.order_ids = t.at("order_ids").get<std::vector<std::string>>();
        value.status = tradeStatusFromName(t.at("status").get<std::string>());
        value.rejection_reason = t.at("rejection_reason").get<std::string>();
        if (t.contains("orphan")) {
            const auto& o = t.at("orphan");
            value.orphan = OrphanExposure{
                venueFromName(o.at("venue").get<std::string>()),
                o.at("market_id").get<std::string>(),
                outcomeFromName(o.at("outcome").get<std::string>()),
                o.at("quantity").get<double>(), o.at("cost_basis").get<double>()};
        }
        engine.trades_.push_back(std::move(value));
    }
    for (const auto& s : json.at("settlements")) engine.settlements_.push_back({
        s.at("settlement_id").get<std::string>(), s.at("pair_key").get<std::string>(),
        outcomeFromName(s.at("winning_outcome").get<std::string>()),
        s.at("payout").get<double>(), s.at("released_cost_basis").get<double>(),
        s.at("realized_pnl").get<double>(), s.at("settlement_time_unix_ms").get<std::int64_t>()
    });
    if (json.contains("open_trades")) {
        for (const auto& t : json.at("open_trades")) {
            OpenConvergenceTrade value;
            value.lifecycle_id = t.at("lifecycle_id").get<std::string>();
            value.entry_trade_id = t.at("entry_trade_id").get<std::string>();
            value.pair_key = t.at("pair_key").get<std::string>();
            value.direction = directionFromName(t.at("direction").get<std::string>());
            value.kalshi_market_id = t.at("kalshi_market_id").get<std::string>();
            value.polymarket_market_id = t.at("polymarket_market_id").get<std::string>();
            value.kalshi_outcome = outcomeFromName(t.at("kalshi_outcome").get<std::string>());
            value.polymarket_outcome =
                outcomeFromName(t.at("polymarket_outcome").get<std::string>());
            value.kalshi_open_quantity = t.at("kalshi_open_quantity").get<double>();
            value.polymarket_open_quantity = t.at("polymarket_open_quantity").get<double>();
            value.kalshi_cost_basis = t.at("kalshi_cost_basis").get<double>();
            value.polymarket_cost_basis = t.at("polymarket_cost_basis").get<double>();
            value.realized_pnl = t.at("realized_pnl").get<double>();
            value.absolute_entry_spread = t.value("absolute_entry_spread", 0.0);
            value.opened_time_unix_ms = t.at("opened_time_unix_ms").get<std::int64_t>();
            const auto& policy = t.at("exit_policy");
            value.exit_policy.minimum_combined_exit_bid =
                policy.at("minimum_combined_exit_bid").get<double>();
            value.exit_policy.profit_target = policy.at("profit_target").get<double>();
            value.exit_policy.stop_loss = policy.at("stop_loss").get<double>();
            value.exit_policy.maximum_hold_ms = policy.at("maximum_hold_ms").get<std::int64_t>();
            value.status = lifecycleStatusFromName(t.at("status").get<std::string>());
            value.closing_order_ids =
                t.at("closing_order_ids").get<std::vector<std::string>>();
            engine.open_trades_.push_back(std::move(value));
        }
    }
    return engine;
}

}  // namespace icarus::paper
