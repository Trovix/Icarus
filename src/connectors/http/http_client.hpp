#pragma once

#include <map>
#include <string>

namespace icarus::connectors {

struct HttpResponse {
    long status_code;
    std::string body;
};

class HttpClient {
public:
    HttpResponse get(
        const std::string& url,
        const std::map<std::string, std::string>& headers = {}
    ) const;
};

}  // namespace icarus::connectors
