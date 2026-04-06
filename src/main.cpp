#include <exception>
#include <iostream>

#include "connectors/http/http_client.hpp"
#include "connectors/kalshi/kalshi_connector.hpp"
#include "connectors/polymarket/polymarket_connector.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

void print_best_price(const char* label, const icarus::core::OrderBookSide& side, std::ostream& out) {
    out << label;
    if (!side.levels.empty()) {
        out << side.levels.front().price << " size " << side.levels.front().size << '\n';
    } else {
        out << "N/A\n";
    }
}

void print_market_snapshot(
    const char* venue_name,
    const icarus::core::Market& market,
    const icarus::core::OrderBook& order_book,
    std::ostream& out
) {
    out << venue_name << " market id: " << market.venue_market_id << '\n';
    out << venue_name << " market title: " << market.title << '\n';
    print_best_price("  yes bid: ", order_book.yes_bids, out);
    print_best_price("  yes ask: ", order_book.yes_asks, out);
    print_best_price("  no bid: ", order_book.no_bids, out);
    print_best_price("  no ask: ", order_book.no_asks, out);
}

void print_market_summary(const char* venue_name, const icarus::core::Market& market, std::ostream& out) {
    out << venue_name << " market id: " << market.venue_market_id << '\n';
    out << venue_name << " market title: " << market.title << '\n';
    out << venue_name << " market active: " << (market.active ? "true" : "false") << '\n';
}

}  // namespace

int main() {
    try {
#ifdef _WIN32
        // Match Windows console output to UTF-8 API text.
        SetConsoleOutputCP(CP_UTF8);
#endif

        icarus::connectors::HttpClient http_client;
        icarus::connectors::kalshi::KalshiConnector kalshi_connector(http_client);
        icarus::connectors::polymarket::PolymarketConnector polymarket_connector(http_client);

        const auto kalshi_markets = kalshi_connector.fetch_markets();
        const auto polymarket_markets = polymarket_connector.fetch_markets();

        std::cout << "ICARUS connector demo\n";
        std::cout << '\n';

        std::cout << "Kalshi fetch_markets(): " << kalshi_markets.size() << " markets\n";
        if (!kalshi_markets.empty()) {
            const auto& kalshi_market = kalshi_markets.front();
            const auto fetched_kalshi_market = kalshi_connector.fetch_market(kalshi_market.venue_market_id);
            const auto kalshi_book = kalshi_connector.fetch_order_book(kalshi_market.venue_market_id);

            std::cout << "Kalshi fetch_market():\n";
            print_market_summary("  ", fetched_kalshi_market, std::cout);
            std::cout << "Kalshi fetch_order_book():\n";
            print_market_snapshot("  ", kalshi_market, kalshi_book, std::cout);
            std::cout << '\n';
        }

        std::cout << "Polymarket fetch_markets(): " << polymarket_markets.size() << " markets\n";
        if (!polymarket_markets.empty()) {
            const auto& polymarket_market = polymarket_markets.front();
            const auto fetched_polymarket_market =
                polymarket_connector.fetch_market(polymarket_market.venue_market_id);
            const auto polymarket_book = polymarket_connector.fetch_order_book(polymarket_market.venue_market_id);

            std::cout << "Polymarket fetch_market():\n";
            print_market_summary("  ", fetched_polymarket_market, std::cout);
            std::cout << "Polymarket fetch_order_book():\n";
            print_market_snapshot("  ", polymarket_market, polymarket_book, std::cout);
            std::cout << '\n';
        }

        std::cout << "First 5 Kalshi titles from fetch_markets():\n";
        for (std::size_t i = 0; i < kalshi_markets.size() && i < 5; ++i) {
            std::cout << "  - " << kalshi_markets[i].title << '\n';
        }
        std::cout << '\n';

        std::cout << "First 5 Polymarket titles from fetch_markets():\n";
        for (std::size_t i = 0; i < polymarket_markets.size() && i < 5; ++i) {
            std::cout << "  - " << polymarket_markets[i].title << '\n';
        }

        return 0;
    } catch (const std::exception& ex) {
        std::cout << "Error: " << ex.what() << '\n';
    }

    return 1;
}
