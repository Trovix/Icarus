#include "app/engine.hpp"

#include <chrono>
#include <iomanip>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "storage/match_store.hpp"
#include "strategy/detector.hpp"

namespace icarus::app {

Engine::Engine(const std::filesystem::path& project_root)
    : project_root_(project_root),
      match_store_path_(project_root_ / "data" / "market_pairs.json"),
      kalshi_index_path_(project_root_ / "data" / "kalshi_market_index.txt"),
      polymarket_index_path_(project_root_ / "data" / "polymarket_market_index.txt"),
      kalshi_connector_(http_client_),
      polymarket_connector_(http_client_) {}

int Engine::run() {
    while (true) {
        std::cout << "\nIcarus:\n";
        std::cout << "[1] Add market pairs\n";
        std::cout << "[2] Load all markets to index files\n";
        std::cout << "[3] Detect opportunities on linked pairs\n";
        std::cout << "[q] Quit\n";
        std::cout << "Choice: ";

        std::string choice;
        if (!std::getline(std::cin, choice)) {
            return 0;
        }

        if (choice == "1") {
            std::vector<storage::MarketMatchIds> match_ids = storage::load_market_matches(match_store_path_);
            prompt_for_manual_pairs(match_ids);
            storage::save_market_matches(match_store_path_, match_ids);
            continue;
        }

        if (choice == "2") {
            export_market_indexes();
            continue;
        }

        if (choice == "3") {
            monitor_saved_pairs();
            return 0;
        }

        if (choice == "q") {
            return 0;
        }
    }
}

void Engine::export_market_indexes() {
    const std::vector<icarus::core::Market> kalshi_markets = kalshi_connector_.fetch_markets();
    const std::vector<icarus::core::Market> polymarket_markets = polymarket_connector_.fetch_markets();

    write_market_indexes(kalshi_markets, polymarket_markets);
    std::cout << "Wrote market indexes:\n";
    std::cout << "  " << kalshi_index_path_.string() << '\n';
    std::cout << "  " << polymarket_index_path_.string() << '\n';
}

void Engine::monitor_saved_pairs() {
    const std::vector<storage::MarketMatchIds> match_ids = storage::load_market_matches(match_store_path_);
    const std::vector<strategy::MarketPair> market_pairs = build_market_pairs(match_ids);

    if (market_pairs.empty()) {
        std::cout << "No matched pairs configured.\n";
        return;
    }

    std::cout << "Monitoring " << market_pairs.size() << " matched pairs.\n";

    while (true) {
        const std::vector<strategy::OpportunitySnapshot> snapshots = fetch_snapshots(market_pairs);
        const std::vector<strategy::DetectedOpportunity> opportunities = fetch_opportunities(market_pairs);
        print_opportunities(opportunities);
        if (opportunities.empty()) {
            print_snapshots(snapshots);
        }
        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

void Engine::write_market_indexes(
    const std::vector<icarus::core::Market>& kalshi_markets,
    const std::vector<icarus::core::Market>& polymarket_markets
) const {
    std::filesystem::create_directories(match_store_path_.parent_path());

    std::ofstream kalshi_output(kalshi_index_path_);
    kalshi_output << "Kalshi markets: " << kalshi_markets.size() << '\n' << '\n';
    for (const icarus::core::Market& market : kalshi_markets) {
        kalshi_output << market.venue_market_id << '\n';
        kalshi_output << market.title << '\n';
        kalshi_output << '\n';
    }

    std::ofstream polymarket_output(polymarket_index_path_);
    polymarket_output << "Polymarket markets: " << polymarket_markets.size() << '\n' << '\n';
    for (const icarus::core::Market& market : polymarket_markets) {
        polymarket_output << market.venue_market_id << '\n';
        polymarket_output << market.title << '\n';
        polymarket_output << '\n';
    }
}

void Engine::prompt_for_manual_pairs(std::vector<storage::MarketMatchIds>& match_ids) {
    std::cout << "Loaded " << match_ids.size() << " saved market pairs.\n";
    std::cout << "Add linked market IDs. Type q to finish.\n\n";

    while (true) {
        std::string kalshi_id;
        std::cout << "Kalshi ID: ";
        if (!std::getline(std::cin, kalshi_id)) {
            return;
        }

        if (kalshi_id == "q" || kalshi_id.empty()) {
            return;
        }

        std::string polymarket_id;
        std::cout << "Polymarket ID: ";
        if (!std::getline(std::cin, polymarket_id)) {
            return;
        }

        if (polymarket_id == "q" || polymarket_id.empty()) {
            return;
        }

        bool duplicate = false;
        for (const storage::MarketMatchIds& existing : match_ids) {
            if (existing.kalshi_id == kalshi_id || existing.polymarket_id == polymarket_id) {
                duplicate = true;
                break;
            }
        }

        if (duplicate) {
            std::cout << "Pair not added: one of those IDs is already linked.\n\n";
            continue;
        }

        const icarus::core::Market kalshi_market = kalshi_connector_.fetch_market(kalshi_id);
        const icarus::core::Market polymarket_market = polymarket_connector_.fetch_market(polymarket_id);

        if (kalshi_market.venue_market_id.empty()) {
            std::cout << "Pair not added: invalid Kalshi ID.\n\n";
            continue;
        }

        if (polymarket_market.venue_market_id.empty()) {
            std::cout << "Pair not added: invalid Polymarket ID.\n\n";
            continue;
        }

        match_ids.push_back({kalshi_id, polymarket_id});
        storage::save_market_matches(match_store_path_, match_ids);

        std::cout << "Added pair:\n";
        std::cout << "  Kalshi: " << kalshi_market.title << '\n';
        std::cout << "  Polymarket: " << polymarket_market.title << '\n';
        std::cout << '\n';
    }
}

std::vector<strategy::MarketPair> Engine::build_market_pairs(
    const std::vector<storage::MarketMatchIds>& match_ids
) {
    std::vector<strategy::MarketPair> market_pairs;

    for (const storage::MarketMatchIds& match_id : match_ids) {
        const icarus::core::Market kalshi_market = kalshi_connector_.fetch_market(match_id.kalshi_id);
        const icarus::core::Market polymarket_market =
            polymarket_connector_.fetch_market(match_id.polymarket_id);

        if (kalshi_market.venue_market_id.empty() || polymarket_market.venue_market_id.empty()) {
            continue;
        }

        market_pairs.push_back({kalshi_market, polymarket_market});
    }

    return market_pairs;
}

std::vector<strategy::DetectedOpportunity> Engine::fetch_opportunities(
    const std::vector<strategy::MarketPair>& market_pairs
) {
    std::vector<strategy::DetectedOpportunity> opportunities;

    for (const strategy::MarketPair& market_pair : market_pairs) {
        const icarus::core::OrderBook kalshi_book =
            kalshi_connector_.fetch_order_book(market_pair.kalshi.venue_market_id);
        const icarus::core::OrderBook polymarket_book =
            polymarket_connector_.fetch_order_book(market_pair.polymarket.venue_market_id);

        const std::vector<strategy::DetectedOpportunity> pair_opportunities =
            strategy::detect_opportunities(market_pair, kalshi_book, polymarket_book);

        opportunities.insert(
            opportunities.end(),
            pair_opportunities.begin(),
            pair_opportunities.end()
        );
    }

    return opportunities;
}

std::vector<strategy::OpportunitySnapshot> Engine::fetch_snapshots(
    const std::vector<strategy::MarketPair>& market_pairs
) {
    std::vector<strategy::OpportunitySnapshot> snapshots;

    for (const strategy::MarketPair& market_pair : market_pairs) {
        const icarus::core::OrderBook kalshi_book =
            kalshi_connector_.fetch_order_book(market_pair.kalshi.venue_market_id);
        const icarus::core::OrderBook polymarket_book =
            polymarket_connector_.fetch_order_book(market_pair.polymarket.venue_market_id);

        const std::optional<strategy::OpportunitySnapshot> snapshot =
            strategy::build_opportunity_snapshot(market_pair, kalshi_book, polymarket_book);

        if (snapshot.has_value()) {
            snapshots.push_back(*snapshot);
        }
    }

    return snapshots;
}

void Engine::print_opportunities(
    const std::vector<strategy::DetectedOpportunity>& opportunities
) const {
    if (opportunities.empty()) {
        std::cout << "[NO OPPORTUNITIES]\n";
        return;
    }

    for (const strategy::DetectedOpportunity& detected : opportunities) {
        const strategy::Opportunity& opportunity = detected.opportunity;
        const double total_cost = opportunity.price_leg_1 + opportunity.price_leg_2;
        const double edge_percent = opportunity.edge * 100.0;
        const double kalshi_odds_percent = opportunity.price_leg_1 * 100.0;
        const double polymarket_odds_percent = opportunity.price_leg_2 * 100.0;

        std::cout << "[OPPORTUNITY]\n";
        std::cout << std::fixed << std::setprecision(4);
        std::cout << "Kalshi ID: " << opportunity.pair.kalshi.venue_market_id << '\n';
        std::cout << "Polymarket ID: " << opportunity.pair.polymarket.venue_market_id << '\n';
        std::cout << "Kalshi Market: " << opportunity.pair.kalshi.title << '\n';
        std::cout << "Polymarket Market: " << opportunity.pair.polymarket.title << '\n';

        if (detected.direction == strategy::OpportunityDirection::KalshiYesPolymarketNo) {
            std::cout << "Direction: Buy Kalshi YES + Buy Polymarket NO\n";
            std::cout << "Kalshi YES ask: " << opportunity.price_leg_1 << '\n';
            std::cout << "Polymarket NO ask: " << opportunity.price_leg_2 << '\n';
        } else {
            std::cout << "Direction: Buy Kalshi NO + Buy Polymarket YES\n";
            std::cout << "Kalshi NO ask: " << opportunity.price_leg_1 << '\n';
            std::cout << "Polymarket YES ask: " << opportunity.price_leg_2 << '\n';
        }

        std::cout << "Kalshi odds: " << kalshi_odds_percent << "%\n";
        std::cout << "Polymarket odds: " << polymarket_odds_percent << "%\n";
        std::cout << "Combined cost: " << total_cost << '\n';
        std::cout << "Edge: " << opportunity.edge << '\n';
        std::cout << "Edge (%): " << edge_percent << '\n';
        std::cout << '\n';
    }
}

void Engine::print_snapshots(const std::vector<strategy::OpportunitySnapshot>& snapshots) const {
    for (const strategy::OpportunitySnapshot& snapshot : snapshots) {
        std::cout << "[PAIR]\n";
        std::cout << "Market: " << snapshot.pair.kalshi.title << '\n';
        std::cout << "Kalshi YES ask: " << snapshot.kalshi_yes << '\n';
        std::cout << "Kalshi NO ask: " << snapshot.kalshi_no << '\n';
        std::cout << "Polymarket YES ask: " << snapshot.polymarket_yes << '\n';
        std::cout << "Polymarket NO ask: " << snapshot.polymarket_no << '\n';
        if (!snapshot.polymarket_yes_token_id.empty() || !snapshot.polymarket_no_token_id.empty()) {
            std::cout << "Polymarket YES token: " << snapshot.polymarket_yes_token_id;
            if (!snapshot.polymarket_yes_label.empty()) {
                std::cout << " (" << snapshot.polymarket_yes_label << ")";
            }
            std::cout << '\n';
            std::cout << "Polymarket NO token: " << snapshot.polymarket_no_token_id;
            if (!snapshot.polymarket_no_label.empty()) {
                std::cout << " (" << snapshot.polymarket_no_label << ")";
            }
            std::cout << '\n';
        }
        std::cout << "YES Kalshi + NO Polymarket: " << snapshot.yes_kalshi_no_polymarket_sum << '\n';
        std::cout << "NO Kalshi + YES Polymarket: " << snapshot.no_kalshi_yes_polymarket_sum << '\n';
        std::cout << '\n';
    }
}

}  // namespace icarus::app
