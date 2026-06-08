#pragma once

#include <filesystem>
#include <string>

namespace pdk::ai {

struct HttpJsonRequest {
    std::string endpoint;
    std::string bearerToken;
    std::string body;
    std::filesystem::path logPath;
    int timeoutMs{30000};
};

struct HttpJsonResponse {
    bool ok{false};
    int statusCode{0};
    std::string body;
    std::string errorMessage;
};

class WinHttpJsonClient {
public:
    HttpJsonResponse Post(const HttpJsonRequest& request) const;

private:
    static void WriteDebugLog(const HttpJsonRequest& request, const HttpJsonResponse& response);
};

} // namespace pdk::ai
