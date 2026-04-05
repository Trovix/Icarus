#include <iostream>

#include "connectors/http/http_client.hpp"
#include "connectors/kalshi/kalshi_connector.hpp"

int main() {
    icarus::connectors::HttpClient http_client;
    icarus::connectors::kalshi::KalshiConnector kalshi_connector(http_client);

    const auto markets = kalshi_connector.fetch_markets();
    std::cout << "Market count: " << markets.size() << '\n';

    if (!markets.empty()) {
        const auto& market = markets.front();
        std::cout << "First market id: " << market.venue_market_id << '\n';
        std::cout << "First market title: " << market.title << '\n';

        const auto order_book = kalshi_connector.fetch_order_book(market.venue_market_id);

        std::cout << "Best yes bid price: ";
        if (!order_book.yes_bids.levels.empty()) {
            std::cout << order_book.yes_bids.levels.front().price << '\n';
        } else {
            std::cout << "N/A\n";
        }

        std::cout << "Best yes ask price: ";
        if (!order_book.yes_asks.levels.empty()) {
            std::cout << order_book.yes_asks.levels.front().price << '\n';
        } else {
            std::cout << "N/A\n";
        }
    }

    return 0;
}
