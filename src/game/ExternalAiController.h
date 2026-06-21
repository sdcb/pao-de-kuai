#pragma once

#include "game/AiStrategy.h"
#include "game/TurnRecord.h"

#include <optional>
#include <vector>

namespace pdk::game {

struct ExternalAiRequest {
    int turnNo{0};
    rules::PlayerId player{rules::PlayerId::Ai1};
    std::string humanName;
    TurnSnapshot snapshot;
    AiContext context;
    std::vector<TurnRecord> history;
};

struct ExternalAiResult {
    bool ok{false};
    GameAction requestedAction;
    std::string reasoningContent;
    std::string toolCallId;
    std::string toolName;
    std::string toolArgumentsJson;
    std::string requestLogPath;
    std::string responseLogPath;
    std::string errorMessage;
    TurnDecisionSource source{TurnDecisionSource::LlmAi};
    std::optional<AiMoveChoice> localChoice;
};

class ExternalAiController {
public:
    virtual ~ExternalAiController() = default;

    virtual bool CanHandle(rules::PlayerId player) const = 0;
    virtual bool IsRemote(rules::PlayerId player) const { return false; }
    virtual bool HasPending() const = 0;
    virtual void Start(ExternalAiRequest request) = 0;
    virtual std::optional<ExternalAiResult> TryGetResult() = 0;
    virtual void Cancel() = 0;
};

} // namespace pdk::game
