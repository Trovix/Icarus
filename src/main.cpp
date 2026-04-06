#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "connectors/http/http_client.hpp"
#include "connectors/kalshi/kalshi_connector.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

int main() {
    try {
#ifdef _WIN32
        // Match Windows console output to UTF-8 API text.
        SetConsoleOutputCP(CP_UTF8);
#endif

        icarus::connectors::HttpClient http_client;
        icarus::connectors::kalshi::KalshiConnector kalshi_connector(http_client);

        const auto markets = kalshi_connector.fetch_markets();
        const std::filesystem::path output_path =
            std::filesystem::current_path().parent_path().parent_path() / "kalshi_markets.txt";
        std::ofstream output_file(output_path);

        output_file << "Market count: " << markets.size() << '\n';

        for (const auto& market : markets) {
            output_file << "Market id: " << market.venue_market_id << '\n';
            output_file << "Market title: " << market.title << '\n';
            output_file << '\n';
        }

        std::cout << "Wrote Kalshi markets to " << output_path.string() << '\n';

        return 0;
    } catch (const std::exception& ex) {
        std::cout << "Error: " << ex.what() << '\n';
    }

    return 1;
}
