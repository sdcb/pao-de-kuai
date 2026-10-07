#pragma once

#include "game/AiStrategy.h"
#include "game/TurnRecord.h"

#include <optional>
#include <vector>

namespace pdk::game {

struct ExternalAiRequest {
    int turnNo{0};
    rules::PlayerId player{PLAYER_AI1};
    std::string humanName;
    TurnSnapshot snapshot;
    AiContext context;
    std::vector<TurnRecord> history;
};

struct ExternalAiResult {
    bool ok{false};
    std::string errorMessage;
    TurnDecisionSource source{TURN_SOURCE_LOCAL_AI};
    std::optional<AiMoveChoice> localChoice;
};

class ExternalAiController {
public:
    virtual ~ExternalAiController() = default;

    virtual bool CanHandle(rules::PlayerId player) const = 0;
    virtual StrategyMetadata MetadataFor(rules::PlayerId player) const { return {"unknown", "unknown"}; }
    virtual bool HasPending() const = 0;
    virtual void Start(ExternalAiRequest request) = 0;
    virtual std::optional<ExternalAiResult> TryGetResult() = 0;
    virtual void Cancel() = 0;
};

} // namespace pdk::game
