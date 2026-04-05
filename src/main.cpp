#include <iostream>
#include <string>
#include <fstream>

#include <windows.h>
#include <filesystem>

#include <nlohmann/json.hpp>
#include "connectors/http/http_client.hpp"

using json = nlohmann::json;

// Clean U+2028 / U+2029 → \n
static void clean_string(std::string& s) {
    for (size_t i = 0; i < s.size(); ++i) {
        if ((unsigned char)s[i] == 0xE2 &&
            i + 2 < s.size() &&
            (unsigned char)s[i+1] == 0x80 &&
            ((unsigned char)s[i+2] == 0xA8 || (unsigned char)s[i+2] == 0xA9)) {

            s.replace(i, 3, "\n");
        }
    }
}

int main() {
    // UTF-8 console
    SetConsoleOutputCP(CP_UTF8);

    HttpClient client;

    // Write to project root (two levels up from build/)
    std::filesystem::path out_path =
        std::filesystem::current_path().parent_path().parent_path() / "markets.txt";

    std::ofstream file(out_path);
    if (!file.is_open()) {
        std::cerr << "Failed to open output file\n";
        return 1;
    }

    try {
        int offset = 0;
        const int limit = 100;
        int count = 0;

        while (true) {
            std::string url =
                "https://gamma-api.polymarket.com/markets"
                "?active=true&closed=false"
                "&limit=" + std::to_string(limit) +
                "&offset=" + std::to_string(offset);

            HttpResponse res = client.get(url);

            if (res.status_code != 200) {
                std::cerr << "HTTP ERROR: " << res.status_code << "\n";
                break;
            }

            json j = json::parse(res.body);

            if (!j.is_array() || j.empty()) {
                break;
            }

            for (const auto& market : j) {
                if ((market.contains("question") && market["question"].is_string()) && (market.contains("id") && market["id"].is_string())) {
                    std::string q = market["question"].get<std::string>();
                    clean_string(q);

                    std::string i = market["id"].get<std::string>();
                    clean_string(i);

                    count++;
                    std::cout << "Found market [" << count << "]\n";

                    
                    file << "- " << i << ": " << q << "\n";
                }

                
            }

            if (j.size() < static_cast<size_t>(limit)) {
                break;
            }

            offset += limit;
        }

        std::cout << "Found all markets\n";

    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
    }

    file.close();
    return 0;
}