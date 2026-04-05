#include <iostream>
#include <exception>

#include "connectors/http/http_client.hpp"
#include "connectors/kalshi/kalshi_connector.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    try {
#ifdef _WIN32
        SetConsoleOutputCP(CP_UTF8);
#endif

        icarus::connectors::HttpClient http_client;
        icarus::connectors::kalshi::KalshiConnector kalshi_connector(http_client);

        const auto markets = kalshi_connector.fetch_markets();
        std::cout << "Market count: " << markets.size() << '\n';

        for (const auto& market : markets) {
            const auto order_book = kalshi_connector.fetch_order_book(market.venue_market_id);
            const bool has_yes_bid = !order_book.yes_bids.levels.empty();
            const bool has_yes_ask = !order_book.yes_asks.levels.empty();
            const bool has_no_bid = !order_book.no_bids.levels.empty();
            const bool has_no_ask = !order_book.no_asks.levels.empty();

            if (!has_yes_bid && !has_yes_ask && !has_no_bid && !has_no_ask) {
                continue;
            }

            std::cout << "First market id: " << market.venue_market_id << '\n';
            std::cout << "First market title: " << market.title << '\n';

            std::cout << "Best yes bid price: ";
            if (has_yes_bid) {
                std::cout << order_book.yes_bids.levels.front().price << '\n';
            } else {
                std::cout << "N/A\n";
            }

            std::cout << "Best yes ask price: ";
            if (has_yes_ask) {
                std::cout << order_book.yes_asks.levels.front().price << '\n';
            } else {
                std::cout << "N/A\n";
            }

            std::cout << "Best no bid price: ";
            if (has_no_bid) {
                std::cout << order_book.no_bids.levels.front().price << '\n';
            } else {
                std::cout << "N/A\n";
            }

            std::cout << "Best no ask price: ";
            if (has_no_ask) {
                std::cout << order_book.no_asks.levels.front().price << '\n';
            } else {
                std::cout << "N/A\n";
            }

            return 0;
        }

        std::cout << "No market with visible order book levels found.\n";

        return 0;
    } catch (const std::exception& ex) {
        std::cout << "Error: " << ex.what() << '\n';
    }

    return 1;
}
