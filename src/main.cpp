#include <iostream>
#include <string>
#include "connectors/http/http_client.hpp"

int main() {
    HttpClient client;

    try {
       
        //geo-block check
        {
            std::cout << "==== Geoblock Check ====\n";

            HttpResponse res = client.get(
                "https://polymarket.com/api/geoblock"
            );

            std::cout << "Status: " << res.status_code << "\n";
            std::cout << "Body:\n" << res.body << "\n\n";
        }

        //fetch markets
        {
            std::cout << "==== Markets (limit 5) ====\n";

            HttpResponse res = client.get(
                "https://gamma-api.polymarket.com/markets?limit=5"
            );

            std::cout << "Status: " << res.status_code << "\n";
            std::cout << "Body:\n" << res.body << "\n\n";
        }

        //fetch events
        {
            std::cout << "==== Events (limit 5) ====\n";

            HttpResponse res = client.get(
                "https://gamma-api.polymarket.com/events?limit=5"
            );

            std::cout << "Status: " << res.status_code << "\n";
            std::cout << "Body:\n" << res.body << "\n\n";
        }

        //fetch token (should fail for now)
        {
            std::cout << "==== Orderbook (example token) ====\n";

            HttpResponse res = client.get(
                "https://clob.polymarket.com/book?token_id=0"
            );

            std::cout << "Status: " << res.status_code << "\n";
            std::cout << "Body:\n" << res.body << "\n\n";
        }

    } catch (const std::exception& e) {
        //curl error 
        std::cerr << "ERROR: " << e.what() << "\n";
    }

    return 0;
}