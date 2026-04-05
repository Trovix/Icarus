#include "http_client.hpp"

#include <curl/curl.h>
#include <map>
#include <string>
#include <stdexcept>

namespace {

size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), total_size);
    return total_size;
}

}  // namespace

namespace icarus::connectors {

HttpResponse HttpClient::get(
    const std::string& url,
    const std::map<std::string, std::string>& headers
) const {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to init curl");
    }

    std::string response_body;
    struct curl_slist* header_list = nullptr;

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    for (const auto& [key, value] : headers) {
        const std::string header = key + ": " + value;
        header_list = curl_slist_append(header_list, header.c_str());
    }
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    CURLcode res = curl_easy_perform(curl);
    if (res != CURLE_OK) {
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        throw std::runtime_error(curl_easy_strerror(res));
    }

    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    return {status_code, response_body};
}

}  // namespace icarus::connectors
