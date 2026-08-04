#include "app/engine.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <future>
#include <iomanip>
#include <iostream>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <thread>

#ifdef _WIN32
#include <conio.h>
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/select.h>
#include <unistd.h>
#endif

#include "storage/market_catalog.hpp"
#include "storage/paper_store.hpp"
#include "strategy/pricing.hpp"

namespace icarus::app {

namespace {

constexpr double kEpsilon = 1e-9;

std::int64_t current_time_unix_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
}

bool is_active(paper::LifecycleStatus status) {
    return status == paper::LifecycleStatus::Open ||
        status == paper::LifecycleStatus::ClosingOrphan;
}

const char* lifecycle_name(paper::LifecycleStatus status) {
    switch (status) {
        case paper::LifecycleStatus::Open: return "open";
        case paper::LifecycleStatus::ClosingOrphan: return "orphan";
        case paper::LifecycleStatus::Closed: return "closed";
        case paper::LifecycleStatus::Settled: return "settled";
    }
    return "unknown";
}

const char* exit_reason_name(paper::ExitReason reason) {
    switch (reason) {
        case paper::ExitReason::None: return "none";
        case paper::ExitReason::Converged: return "converged";
        case paper::ExitReason::ProfitTarget: return "profit target";
        case paper::ExitReason::StopLoss: return "stop loss";
        case paper::ExitReason::MaximumHold: return "maximum hold";
        case paper::ExitReason::Forced: return "forced";
    }
    return "unknown";
}

std::string quoted(const std::filesystem::path& path) {
    return "\"" + path.string() + "\"";
}

std::string quoted(const std::string& value) {
    return "\"" + value + "\"";
}

std::optional<char> console_character() {
#ifdef _WIN32
    if (_kbhit()) {
        return static_cast<char>(_getch());
    }
#else
    timeval timeout{};
    fd_set descriptors;
    FD_ZERO(&descriptors);
    FD_SET(STDIN_FILENO, &descriptors);
    if (select(STDIN_FILENO + 1, &descriptors, nullptr, nullptr, &timeout) > 0) {
        char value = 0;
        if (::read(STDIN_FILENO, &value, 1) == 1) {
            return value;
        }
    }
#endif
    return std::nullopt;
}

double average_debit(const paper::BookWalk& walk) {
    if (walk.filled_quantity <= kEpsilon) {
        return std::numeric_limits<double>::infinity();
    }
    return (walk.gross_notional + walk.fee) / walk.filled_quantity;
}

}  // namespace

Engine::Engine(const std::filesystem::path& project_root)
    : project_root_(project_root),
      settings_path_(project_root_ / "config" / "paper.json"),
      paper_state_path_(project_root_ / "data" / "paper_state.json"),
      match_store_path_(project_root_ / "data" / "market_pairs.json"),
      kalshi_catalog_path_(project_root_ / "data" / "kalshi_markets.json"),
      polymarket_catalog_path_(project_root_ / "data" / "polymarket_markets.json"),
      kalshi_index_path_(project_root_ / "data" / "kalshi_market_index.txt"),
      polymarket_index_path_(project_root_ / "data" / "polymarket_market_index.txt"),
      kalshi_connector_(http_client_),
      polymarket_connector_(http_client_) {}

int Engine::run() {
    if (!initialize()) {
        return 1;
    }

    render_dashboard();
    while (running_) {
        const auto cycle_started = std::chrono::steady_clock::now();
        if (!paused_) {
            process_cycle();
            render_dashboard();
        }

        const auto deadline = cycle_started +
            std::chrono::milliseconds(settings_.poll_interval_ms);
        while (running_ && std::chrono::steady_clock::now() < deadline) {
            if (handle_console_input()) {
                render_dashboard();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    storage::save_paper_state(paper_state_path_, *paper_engine_);
    std::cout << "\nPaper state saved.\n";
    return 0;
}

bool Engine::initialize() {
    try {
        settings_ = load_settings(settings_path_);
        const auto saved = storage::load_paper_state(paper_state_path_);
        if (saved) {
            paper_engine_ = std::make_unique<paper::PaperTradingEngine>(std::move(*saved));
            paper_engine_->reconfigure(settings_.paper);
            add_event("Recovered paper portfolio from disk");
        } else {
            paper_engine_ = std::make_unique<paper::PaperTradingEngine>(
                settings_.paper,
                std::map<core::Venue, double>{
                    {core::Venue::Kalshi, settings_.starting_cash_kalshi},
                    {core::Venue::Polymarket, settings_.starting_cash_polymarket},
                }
            );
            add_event("Created a new paper portfolio");
        }

        if (settings_.auto_match && catalogs_need_refresh()) {
            refresh_catalogs_and_pairs();
        }

        if (market_pairs_.empty() && !load_market_pairs(true)) {
            add_event("No usable high-confidence pairs are available");
            std::cerr << "No usable market pairs. Check catalogs, matcher output, and API access.\n";
            return false;
        }
        return true;
    } catch (const std::exception& error) {
        std::cerr << "Initialization failed: " << error.what() << '\n';
        return false;
    }
}

bool Engine::catalogs_need_refresh() const {
    const auto stale = [&](const std::filesystem::path& path) {
        std::error_code error;
        const auto written = std::filesystem::last_write_time(path, error);
        if (error) {
            return true;
        }
        return std::filesystem::file_time_type::clock::now() - written >
            std::chrono::milliseconds(settings_.catalog_refresh_interval_ms);
    };
    return stale(kalshi_catalog_path_) || stale(polymarket_catalog_path_);
}

bool Engine::refresh_catalogs_and_pairs() {
    try {
        add_event("Refreshing public market catalogs");
        export_market_catalogs();
        if (!run_matcher()) {
            add_event("Matcher failed; retaining the previous pair store");
            return false;
        }
        if (!load_market_pairs(false)) {
            add_event("Matcher produced no usable pairs; retaining the active pair set");
            return false;
        }
        add_event("Autonomous market matching completed");
        return true;
    } catch (const std::exception& error) {
        add_event(std::string("Catalog refresh failed: ") + error.what());
        return false;
    }
}

bool Engine::load_market_pairs(bool required) {
    try {
        const auto match_ids = storage::load_market_matches(match_store_path_);
        auto candidate_pairs = build_market_pairs(match_ids);
        // A refreshed matcher file contains active markets only. Preserve a
        // minimal pair descriptor for every recovered position until it exits
        // or settles, even when that market has disappeared from new catalogs.
        for (const auto& lifecycle : paper_engine_->openTrades()) {
            if (!is_active(lifecycle.status)) {
                continue;
            }
            const auto existing = std::find_if(
                candidate_pairs.begin(), candidate_pairs.end(),
                [&](const strategy::MarketPair& pair) {
                    return paper::marketPairKey(pair) == lifecycle.pair_key;
                }
            );
            if (existing == candidate_pairs.end()) {
                candidate_pairs.push_back({
                    core::Market{
                        core::Venue::Kalshi, lifecycle.kalshi_market_id,
                        "Recovered open position", false,
                    },
                    core::Market{
                        core::Venue::Polymarket, lifecycle.polymarket_market_id,
                        "Recovered open position", false,
                    },
                    "recovered:" + lifecycle.lifecycle_id,
                    lifecycle.outcomes_aligned,
                    1.0,
                });
            }
        }
        if (candidate_pairs.empty()) {
            if (required) {
                add_event("No usable high-confidence pairs are available");
            }
            return false;
        }

        market_pairs_ = std::move(candidate_pairs);
        std::error_code error;
        match_store_write_time_ = std::filesystem::last_write_time(
            match_store_path_, error
        );
        if (error) {
            match_store_write_time_ = {};
        }
        last_pair_reload_check_unix_ms_ = current_time_unix_ms();
        add_event("Loaded " + std::to_string(market_pairs_.size()) + " market pairs");
        return true;
    } catch (const std::exception& error) {
        if (required) {
            throw;
        }
        add_event(std::string("Pair reload failed; retaining active pairs: ") + error.what());
        return false;
    }
}

void Engine::maintain_market_pairs() {
    if (settings_.auto_match && catalogs_need_refresh()) {
        refresh_catalogs_and_pairs();
        return;
    }

    const std::int64_t now = current_time_unix_ms();
    if (now - last_pair_reload_check_unix_ms_ < settings_.pair_reload_interval_ms) {
        return;
    }
    last_pair_reload_check_unix_ms_ = now;

    std::error_code error;
    const auto written = std::filesystem::last_write_time(match_store_path_, error);
    if (!error && written != match_store_write_time_) {
        load_market_pairs(false);
    }
}

bool Engine::run_matcher() const {
    const char* configured_python = std::getenv("ICARUS_PYTHON");
    const std::string python = configured_python && *configured_python
        ? configured_python
        : "python";
    const std::filesystem::path generated = match_store_path_.string() + ".generated";
    std::string command = quoted(python) + " " + quoted(project_root_ / "matcher" / "run.py") +
        " --kalshi-catalog " + quoted(kalshi_catalog_path_) +
        " --polymarket-catalog " + quoted(polymarket_catalog_path_) +
        " --output " + quoted(generated) +
        " --cache " + quoted(project_root_ / "data" / "matcher_cache.sqlite3");
    if (!std::getenv("OPENAI_API_KEY") && settings_.allow_offline_matching) {
        command += " --offline";
    } else if (!std::getenv("OPENAI_API_KEY")) {
        throw std::runtime_error(
            "OPENAI_API_KEY is required for semantic matching; set "
            "allow_offline_matching=true only for an explicit lexical-only run"
        );
    }

    const int result = std::system(command.c_str());
    if (result != 0) {
        return false;
    }
    const auto generated_matches = storage::load_market_matches(generated);
    if (generated_matches.empty()) {
        std::error_code error;
        std::filesystem::remove(generated, error);
        return false;
    }

#ifdef _WIN32
    if (!MoveFileExW(
            generated.c_str(),
            match_store_path_.c_str(),
            MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        )) {
        return false;
    }
#else
    std::error_code error;
    std::filesystem::rename(generated, match_store_path_, error);
    if (error) {
        return false;
    }
#endif
    return true;
}

void Engine::export_market_catalogs() {
    auto kalshi_future = std::async(std::launch::async, [&] {
        return kalshi_connector_.fetch_markets();
    });
    auto polymarket_future = std::async(std::launch::async, [&] {
        return polymarket_connector_.fetch_markets();
    });
    const auto kalshi_markets = kalshi_future.get();
    const auto polymarket_markets = polymarket_future.get();
    if (kalshi_markets.empty() || polymarket_markets.empty()) {
        throw std::runtime_error("a venue returned an empty market catalog");
    }
    if (!storage::export_market_catalog_atomic(kalshi_catalog_path_, kalshi_markets) ||
        !storage::export_market_catalog_atomic(polymarket_catalog_path_, polymarket_markets)) {
        throw std::runtime_error("unable to write canonical market catalogs");
    }
    write_market_indexes(kalshi_markets, polymarket_markets);
}

void Engine::write_market_indexes(
    const std::vector<core::Market>& kalshi_markets,
    const std::vector<core::Market>& polymarket_markets
) const {
    std::filesystem::create_directories(match_store_path_.parent_path());
    std::ofstream kalshi_output(kalshi_index_path_);
    std::ofstream polymarket_output(polymarket_index_path_);
    if (!kalshi_output || !polymarket_output) {
        throw std::runtime_error("unable to write human-readable market indexes");
    }
    kalshi_output << "Kalshi markets: " << kalshi_markets.size() << "\n\n";
    for (const auto& market : kalshi_markets) {
        kalshi_output << market.venue_market_id << '\n' << market.title << "\n\n";
    }
    polymarket_output << "Polymarket markets: " << polymarket_markets.size() << "\n\n";
    for (const auto& market : polymarket_markets) {
        polymarket_output << market.venue_market_id << '\n' << market.title << "\n\n";
    }
}

std::vector<strategy::MarketPair> Engine::build_market_pairs(
    const std::vector<storage::MarketMatchIds>& match_ids
) {
    std::vector<strategy::MarketPair> pairs;
    for (const auto& match : match_ids) {
        if (match.confidence + kEpsilon < settings_.minimum_match_confidence) {
            continue;
        }
        if (!settings_.allow_offline_matching && match.source != "openai_judged") {
            continue;
        }
        auto kalshi_future = std::async(std::launch::async, [&] {
            return kalshi_connector_.fetch_market(match.kalshi_id);
        });
        auto polymarket_future = std::async(std::launch::async, [&] {
            return polymarket_connector_.fetch_market(match.polymarket_id);
        });
        core::Market kalshi_market = kalshi_future.get();
        core::Market polymarket_market = polymarket_future.get();
        if (kalshi_market.venue_market_id.empty() ||
            polymarket_market.venue_market_id.empty() ||
            !kalshi_market.active || !polymarket_market.active) {
            add_event("Skipped unavailable pair " + match.pair_id);
            continue;
        }
        pairs.push_back({
            std::move(kalshi_market),
            std::move(polymarket_market),
            match.pair_id,
            match.yes_maps_to == "yes",
            match.confidence,
        });
    }
    return pairs;
}

bool Engine::process_cycle() {
    maintain_market_pairs();
    latest_snapshots_.clear();
    latest_marks_.clear();
    bool changed = false;
    std::size_t healthy_pairs = 0;
    for (const auto& pair : market_pairs_) {
        try {
            if (process_pair(pair)) {
                changed = true;
            }
            ++healthy_pairs;
        } catch (const std::exception& error) {
            add_event("Pair " + pair.pair_id + " failed: " + error.what());
        }
    }
    venue_status_ = healthy_pairs == market_pairs_.size()
        ? "healthy"
        : std::to_string(healthy_pairs) + "/" +
            std::to_string(market_pairs_.size()) + " pairs healthy";
    if (changed) {
        storage::save_paper_state(paper_state_path_, *paper_engine_);
    }
    return changed;
}

bool Engine::process_pair(const strategy::MarketPair& pair) {
    auto kalshi_future = std::async(std::launch::async, [&] {
        return kalshi_connector_.fetch_order_book(pair.kalshi.venue_market_id);
    });
    auto polymarket_future = std::async(std::launch::async, [&] {
        return polymarket_connector_.fetch_order_book(pair.polymarket.venue_market_id);
    });
    const core::OrderBook kalshi_book = kalshi_future.get();
    const core::OrderBook polymarket_book = polymarket_future.get();
    if (kalshi_book.venue_market_id.empty() ||
        polymarket_book.venue_market_id.empty()) {
        if (try_settle_pair(pair)) {
            return true;
        }
        throw std::runtime_error("one or both executable books are unavailable");
    }
    add_marks(pair, kalshi_book, polymarket_book);

    const std::string pair_key = paper::marketPairKey(pair);
    std::vector<std::string> open_lifecycle_ids;
    for (const auto& lifecycle : paper_engine_->openTrades()) {
        if (lifecycle.pair_key == pair_key && is_active(lifecycle.status)) {
            open_lifecycle_ids.push_back(lifecycle.lifecycle_id);
        }
    }

    bool changed = false;
    bool exited_this_cycle = false;
    for (const auto& lifecycle_id : open_lifecycle_ids) {
        paper::ConvergenceCloseRequest close;
        close.request_id = next_request_id("exit");
        close.lifecycle_id = lifecycle_id;
        close.evaluation_time_unix_ms = current_time_unix_ms();
        close.kalshi_book = kalshi_book;
        close.polymarket_book = polymarket_book;
        const auto result = paper_engine_->evaluateAndClose(close);
        if (result.triggered) {
            changed = true;
            exited_this_cycle = true;
            std::ostringstream event;
            event << "Exit " << lifecycle_id << " ("
                  << exit_reason_name(result.reason) << ") realized " << std::fixed
                  << std::setprecision(2) << result.realized_pnl;
            if (!result.fully_closed) {
                event << " (partial/orphan remains)";
            }
            add_event(event.str());
        }
    }

    // Entry analysis needs complete two-sided books, but held-leg exits above do
    // not. A missing unrelated ask must never block a stop or timed liquidation.
    const auto snapshot = strategy::build_convergence_snapshot(
        pair, kalshi_book, polymarket_book
    );
    if (snapshot) {
        latest_snapshots_.push_back(*snapshot);
    }

    const bool still_open = std::any_of(
        paper_engine_->openTrades().begin(),
        paper_engine_->openTrades().end(),
        [&](const paper::OpenConvergenceTrade& lifecycle) {
            return lifecycle.pair_key == pair_key && is_active(lifecycle.status);
        }
    );
    if (still_open || exited_this_cycle || !snapshot) {
        return changed;
    }

    struct EntryCandidate {
        strategy::OpportunityDirection direction;
        double paired_quantity;
        double unit_debit;
        double executable_edge;
    };
    const auto candidate = [&](strategy::OpportunityDirection direction) {
        const paper::Outcome kalshi_outcome =
            direction == strategy::OpportunityDirection::KalshiYesPolymarketNo
            ? paper::Outcome::Yes
            : paper::Outcome::No;
        const paper::Outcome polymarket_outcome = pair.outcomes_aligned
            ? (kalshi_outcome == paper::Outcome::Yes
                ? paper::Outcome::No : paper::Outcome::Yes)
            : kalshi_outcome;
        const auto kalshi_capacity = paper::walkBuyBook(
            kalshi_book, kalshi_outcome, settings_.trade_quantity, 1.0,
            std::numeric_limits<double>::max(), settings_.paper.kalshi
        );
        const auto polymarket_capacity = paper::walkBuyBook(
            polymarket_book, polymarket_outcome, settings_.trade_quantity, 1.0,
            std::numeric_limits<double>::max(), settings_.paper.polymarket
        );
        const double paired_quantity = std::min(
            kalshi_capacity.filled_quantity, polymarket_capacity.filled_quantity
        );
        if (paired_quantity <= kEpsilon) {
            return EntryCandidate{direction, 0.0,
                                  std::numeric_limits<double>::infinity(),
                                  -std::numeric_limits<double>::infinity()};
        }

        // Re-walk the common paired prefix. Averaging independently filled
        // depths can include levels that would never be part of the trade.
        const auto kalshi_walk = paper::walkBuyBook(
            kalshi_book, kalshi_outcome, paired_quantity, 1.0,
            std::numeric_limits<double>::max(), settings_.paper.kalshi
        );
        const auto polymarket_walk = paper::walkBuyBook(
            polymarket_book, polymarket_outcome, paired_quantity, 1.0,
            std::numeric_limits<double>::max(), settings_.paper.polymarket
        );
        const double unit_debit =
            average_debit(kalshi_walk) + average_debit(polymarket_walk);
        return EntryCandidate{
            direction, paired_quantity, unit_debit, 1.0 - unit_debit,
        };
    };

    const EntryCandidate first = candidate(
        strategy::OpportunityDirection::KalshiYesPolymarketNo
    );
    const EntryCandidate second = candidate(
        strategy::OpportunityDirection::KalshiNoPolymarketYes
    );
    const EntryCandidate& best = first.executable_edge >= second.executable_edge
        ? first : second;
    if (best.paired_quantity <= kEpsilon ||
        best.executable_edge + kEpsilon < settings_.entry_spread_threshold) {
        return changed;
    }

    paper::OpenConvergenceRequest open;
    open.entry.request_id = next_request_id("entry");
    open.entry.pair = pair;
    open.entry.direction = best.direction;
    open.entry.quantity = std::min(settings_.trade_quantity, best.paired_quantity);
    open.entry.maximum_price_per_leg = 1.0;
    open.entry.signal_time_unix_ms = current_time_unix_ms();
    open.entry.kalshi_book = kalshi_book;
    open.entry.polymarket_book = polymarket_book;
    open.exit_policy.minimum_combined_exit_bid = settings_.minimum_combined_exit_bid;
    open.exit_policy.profit_target = settings_.profit_target;
    open.exit_policy.stop_loss = settings_.stop_loss;
    open.exit_policy.maximum_hold_ms = settings_.maximum_hold_ms;
    open.absolute_entry_spread = best.executable_edge;
    open.minimum_entry_spread = settings_.entry_spread_threshold;
    const auto result = paper_engine_->openConvergence(open);
    if (result.accepted && result.trade) {
        changed = true;
        std::ostringstream event;
        event << "Opened " << pair.pair_id << " executable edge=" << std::fixed
              << std::setprecision(3) << best.executable_edge << " quantity="
              << result.trade->paired_quantity;
        add_event(event.str());
    } else if (!result.accepted) {
        add_event("Entry rejected for " + pair.pair_id + ": " + result.reason);
    }
    return changed;
}

bool Engine::try_settle_pair(const strategy::MarketPair& pair) {
    const std::string pair_key = paper::marketPairKey(pair);
    const bool has_open_position = std::any_of(
        paper_engine_->openTrades().begin(), paper_engine_->openTrades().end(),
        [&](const paper::OpenConvergenceTrade& lifecycle) {
            return lifecycle.pair_key == pair_key && is_active(lifecycle.status);
        }
    );
    if (!has_open_position) {
        return false;
    }

    auto kalshi_future = std::async(std::launch::async, [&] {
        return kalshi_connector_.fetch_market(pair.kalshi.venue_market_id);
    });
    auto polymarket_future = std::async(std::launch::async, [&] {
        return polymarket_connector_.fetch_market(pair.polymarket.venue_market_id);
    });
    const core::Market kalshi = kalshi_future.get();
    const core::Market polymarket = polymarket_future.get();
    if ((kalshi.result != "yes" && kalshi.result != "no") ||
        (polymarket.result != "yes" && polymarket.result != "no")) {
        return false;
    }

    std::string polymarket_semantic_result = polymarket.result;
    if (!pair.outcomes_aligned) {
        polymarket_semantic_result = polymarket.result == "yes" ? "no" : "yes";
    }
    if (kalshi.result != polymarket_semantic_result) {
        add_event("Settlement withheld for " + pair.pair_id +
                  ": venues report conflicting outcomes");
        return false;
    }

    const paper::Outcome winner = kalshi.result == "yes"
        ? paper::Outcome::Yes : paper::Outcome::No;
    const auto settlement = paper_engine_->settle(
        pair_key, winner, current_time_unix_ms()
    );
    std::ostringstream event;
    event << "Settled " << pair.pair_id << " payout=" << std::fixed
          << std::setprecision(2) << settlement.payout
          << " P&L=" << settlement.realized_pnl;
    add_event(event.str());
    return true;
}

void Engine::add_marks(
    const strategy::MarketPair& pair,
    const core::OrderBook& kalshi_book,
    const core::OrderBook& polymarket_book
) {
    const std::string pair_key = paper::marketPairKey(pair);
    const auto add_book_marks = [&](const core::OrderBook& book) {
        const auto top = strategy::extract_top_of_book(book);
        if (top.best_yes_bid) {
            latest_marks_.push_back({
                pair_key, book.venue, book.venue_market_id,
                paper::Outcome::Yes, *top.best_yes_bid,
            });
        }
        if (top.best_no_bid) {
            latest_marks_.push_back({
                pair_key, book.venue, book.venue_market_id,
                paper::Outcome::No, *top.best_no_bid,
            });
        }
    };
    add_book_marks(kalshi_book);
    add_book_marks(polymarket_book);
}

void Engine::render_dashboard() const {
    const auto portfolio = paper_engine_->portfolio(latest_marks_);
    std::size_t open_count = 0;
    for (const auto& lifecycle : paper_engine_->openTrades()) {
        if (is_active(lifecycle.status)) {
            ++open_count;
        }
    }

    std::cout << "\x1b[2J\x1b[H";
    std::cout << "ICARUS - AUTONOMOUS CONVERGENCE PAPER TRADING\n";
    std::cout << "State: " << (paused_ ? "PAUSED" : "RUNNING")
              << " | Data: " << venue_status_
              << " | Pairs: " << market_pairs_.size()
              << " | Open: " << open_count << "\n\n";
    std::cout << std::fixed << std::setprecision(2);
    std::cout << "Cash  Kalshi: " << paper_engine_->cash(core::Venue::Kalshi)
              << "  Polymarket: " << paper_engine_->cash(core::Venue::Polymarket)
              << "  Total equity: " << portfolio.total_equity << '\n';
    std::cout << "P&L   Realized: " << portfolio.realized_pnl
              << "  Executable unrealized: " << portfolio.unrealized_pnl
              << "  Open basis: " << portfolio.open_cost_basis << "\n\n";

    std::cout << "Latest spreads\n";
    std::cout << std::setprecision(3);
    const std::size_t snapshot_start = latest_snapshots_.size() > 8
        ? latest_snapshots_.size() - 8
        : 0;
    for (std::size_t index = snapshot_start; index < latest_snapshots_.size(); ++index) {
        const auto& snapshot = latest_snapshots_[index];
        std::cout << "  " << snapshot.pair.pair_id
                  << "  S=" << snapshot.spread
                  << "  K=" << snapshot.kalshi_yes_reference
                  << "  P=" << snapshot.polymarket_yes_reference << '\n';
    }

    std::cout << "\nOpen lifecycles\n";
    const std::int64_t now = current_time_unix_ms();
    for (const auto& lifecycle : paper_engine_->openTrades()) {
        if (!is_active(lifecycle.status)) {
            continue;
        }
        std::cout << "  " << lifecycle.lifecycle_id << " "
                  << lifecycle.pair_key << " " << lifecycle_name(lifecycle.status)
                  << " entry spread=" << lifecycle.absolute_entry_spread
                  << " age=" << std::max<std::int64_t>(
                         0, now - lifecycle.opened_time_unix_ms
                     ) / 1000 << "s"
                  << " Kqty=" << lifecycle.kalshi_open_quantity
                  << " Pqty=" << lifecycle.polymarket_open_quantity;
        const auto snapshot = std::find_if(
            latest_snapshots_.begin(), latest_snapshots_.end(),
            [&](const strategy::ConvergenceSnapshot& value) {
                return paper::marketPairKey(value.pair) == lifecycle.pair_key;
            }
        );
        if (snapshot != latest_snapshots_.end()) {
            std::cout << " current S=" << snapshot->spread;
        }
        std::cout << '\n';
    }

    std::cout << "\nRecent events\n";
    for (const auto& event : recent_events_) {
        std::cout << "  " << event << '\n';
    }
    std::cout << "\n[p] pause/resume  [r] run cycle  [m] rematch  [q] save and quit\n";
    std::cout.flush();
}

void Engine::add_event(const std::string& message) {
    recent_events_.push_back("[" + std::to_string(current_time_unix_ms()) + "] " + message);
    if (recent_events_.size() > 8) {
        recent_events_.erase(recent_events_.begin());
    }
}

bool Engine::handle_console_input() {
    bool changed = false;
    while (const auto character = console_character()) {
        if (*character == 'q' || *character == 'Q') {
            running_ = false;
            changed = true;
        } else if (*character == 'p' || *character == 'P') {
            paused_ = !paused_;
            add_event(paused_ ? "Engine paused" : "Engine resumed");
            changed = true;
        } else if (*character == 'r' || *character == 'R') {
            process_cycle();
            add_event("Manual cycle completed");
            changed = true;
        } else if (*character == 'm' || *character == 'M') {
            if (refresh_catalogs_and_pairs()) {
                add_event("Manual catalog and pair refresh completed");
            } else {
                add_event("Manual rematch failed; existing pairs retained");
            }
            changed = true;
        }
    }
    return changed;
}

std::string Engine::next_request_id(const char* prefix) {
    return std::string(prefix) + "-" + std::to_string(current_time_unix_ms()) + "-" +
        std::to_string(request_sequence_++);
}

}  // namespace icarus::app
