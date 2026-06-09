#pragma once

#include "rules/Card.h"
#include "rules/HandPattern.h"
#include "rules/Scoring.h"

#include <array>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace pdk::game {

enum class TurnDecisionSource {
    Human,
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

struct GameAction {
    std::string action;
    std::vector<std::string> ranks;
    std::string talk;
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
    std::string toolCallId;
    std::string toolName;
    std::string toolArgumentsJson;
    std::string toolResultJson;
    std::filesystem::path requestLogPath;
    std::filesystem::path responseLogPath;
    std::string errorMessage;
};

struct TurnRecord {
    int turnNo{0};
    rules::PlayerId actor{rules::PlayerId::Player};
    TurnDecisionSource source{TurnDecisionSource::LocalAi};
    TurnDecisionReason reason{TurnDecisionReason::NormalChoice};
    TurnSnapshot before;
    TurnSnapshot after;
    GameAction requestedAction;
    GameAction finalAction;
    rules::Cards finalCards;
    std::optional<rules::HandPattern> finalPattern;
    bool accepted{true};
    std::string validationMessage;
    TurnDecisionTrace trace;
};

std::string PlayerLabel(rules::PlayerId player);
std::string SourceLabel(TurnDecisionSource source);
std::string ReasonLabel(TurnDecisionReason reason);
std::string ActionArgumentsJson(const GameAction& action);
std::string ForcedMoveArgumentsJson(TurnDecisionReason reason, const GameAction& action);

} // namespace pdk::game
