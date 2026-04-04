#include "http_client.hpp"
#include <curl/curl.h>
#include <stdexcept>

HttpClient::HttpClient() {
    curl_global_init(CURL_GLOBAL_DEFAULT);
}

HttpClient::~HttpClient() {
    curl_global_cleanup();
}

size_t HttpClient::write_callback(void* contents, size_t size, size_t nmemb, void* userp) {
    size_t total_size = size * nmemb;
    std::string* response = static_cast<std::string*>(userp);
    response->append(static_cast<char*>(contents), total_size);
    return total_size;
}

HttpResponse HttpClient::get(
    const std::string& url,
    const std::vector<std::string>& headers,
    long timeout_ms
) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to init curl");
    }

    std::string response_body;
    struct curl_slist* header_list = nullptr;

    // set URL
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());

    // write callback
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);

    // timeout
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, timeout_ms);

    // follow redirects
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);

    // headers
    for (const auto& h : headers) {
        header_list = curl_slist_append(header_list, h.c_str());
    }
    if (header_list) {
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, header_list);
    }

    // perform request
    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
        curl_slist_free_all(header_list);
        curl_easy_cleanup(curl);
        throw std::runtime_error(curl_easy_strerror(res));
    }

    // get HTTP status
    long status_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status_code);

    // cleanup
    curl_slist_free_all(header_list);
    curl_easy_cleanup(curl);

    return {status_code, response_body};
}