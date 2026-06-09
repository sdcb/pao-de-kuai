#pragma once

#include "ai/PdkAiClient.h"
#include "rules/Card.h"
#include "rules/HandPattern.h"
#include "rules/Scoring.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace pdk::ai {

enum class TurnDecisionSource {
    LocalAi,
    LlmAi,
    System
};

enum class TurnDecisionReason {
    NormalChoice,
    CannotBeat,
    OnlyLegalMove,
    LlmFallback
};

struct TurnSnapshot {
    std::array<rules::Cards, 3> hands;
    rules::Cards lastCards;
    std::optional<rules::HandPattern> lastPattern;
    rules::PlayerId lastMovePlayer{rules::PlayerId::Player};
    rules::PlayerId currentPlayer{rules::PlayerId::Player};
    int passCount{0};
};

struct TurnDecisionTrace {
    std::string reasoningContent;
    PdkAiMessage assistantMessage;
    std::optional<PdkAiMessage> toolMessage;
    std::string requestLogPath;
    std::string responseLogPath;
};

struct TurnRecord {
    int turnNo{0};
    rules::PlayerId actor{rules::PlayerId::Player};
    TurnDecisionSource source{TurnDecisionSource::LocalAi};
    TurnDecisionReason reason{TurnDecisionReason::NormalChoice};
    TurnSnapshot before;
    TurnSnapshot after;
    PdkAiMove requestedAction;
    PdkAiMove finalAction;
    rules::Cards finalCards;
    std::optional<rules::HandPattern> finalPattern;
    bool accepted{true};
    std::string validationMessage;
    TurnDecisionTrace trace;
};

std::string ToJson(const std::vector<TurnRecord>& records);
std::string PlayerLabel(rules::PlayerId player);
std::string SourceLabel(TurnDecisionSource source);
std::string ReasonLabel(TurnDecisionReason reason);

} // namespace pdk::ai
