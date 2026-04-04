#pragma once

#include <string>
#include <vector>

struct HttpResponse {
    long status_code;
    std::string body;
};

class HttpClient {
public:
    HttpClient();
    ~HttpClient();

    HttpResponse get(
        const std::string& url,
        const std::vector<std::string>& headers = {},
        long timeout_ms = 5000
    );

private:
    static size_t write_callback(void* contents, size_t size, size_t nmemb, void* userp);
};