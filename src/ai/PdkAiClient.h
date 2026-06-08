#pragma once

#include "ai/WinHttpJsonClient.h"
#include "stats/AppSettings.h"

#include <filesystem>
#include <string>
#include <vector>

namespace pdk::ai {

struct PdkAiToolCall {
    std::string id;
    std::string name{"choose_move"};
    std::string argumentsJson;
};

struct PdkAiMessage {
    std::string role;
    std::string content;
    std::string reasoningContent;
    std::string toolCallId;
    std::string name;
    std::vector<PdkAiToolCall> toolCalls;
};

struct PdkAiMove {
    std::string action;
    std::vector<std::string> ranks;
    std::string talk;
};

struct PdkAiRequest {
    stats::AiProviderSettings provider;
    std::vector<PdkAiMessage> messages;
    std::filesystem::path requestLogPath;
    std::filesystem::path responseLogPath;
    int timeoutMs{30000};
};

struct PdkAiResponse {
    bool ok{false};
    PdkAiMove move;
    std::string reasoningContent;
    PdkAiMessage assistantMessage;
    std::string rawResponse;
    std::string errorMessage;
};

class PdkAiClient {
public:
    PdkAiResponse ChooseMove(const PdkAiRequest& request) const;

    static std::string BuildRequestJson(const PdkAiRequest& request);
    static PdkAiResponse ParseResponse(std::string responseBody);
    static std::string BuildToolResultJson(const PdkAiMove& move, bool accepted, const std::string& reason);

private:
    WinHttpJsonClient http_;
};

} // namespace pdk::ai
