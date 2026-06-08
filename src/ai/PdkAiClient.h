#pragma once

#include "ai/WinHttpJsonClient.h"
#include "stats/AppSettings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace pdk::ai {

struct PdkAiOption {
    int index{};
    std::string summary;
};

struct PdkAiRequest {
    stats::AiProviderSettings provider;
    std::string systemPrompt;
    std::string contextText;
    std::vector<PdkAiOption> options;
    std::filesystem::path logPath;
};

struct PdkAiResponse {
    bool ok{false};
    int selectedIndex{-1};
    std::string talk;
    std::string reasoningContent;
    std::string rawContent;
    std::string rawResponse;
    std::string errorMessage;
};

class PdkAiClient {
public:
    PdkAiResponse Choose(const PdkAiRequest& request) const;

    static std::string BuildRequestJson(const PdkAiRequest& request);
    static PdkAiResponse ParseResponse(std::string responseBody, const std::vector<PdkAiOption>& options);

private:
    WinHttpJsonClient http_;
};

} // namespace pdk::ai
