#pragma once

#include "game/StrategyMetadata.h"
#include "rules/CppCompat.h"
#include "rules/CppCompat.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace pdk::game {

enum class TurnDecisionSource {
    Human,
    LocalAi,
    System
};

enum class TurnDecisionReason {
    NormalChoice,
    CannotBeat,
    OnlyLegalMove
};

struct GameAction {
    std::string action;
    std::vector<std::string> ranks;
};

struct TurnSnapshot {
    std::array<rules::Cards, 3> hands;
    rules::Cards lastCards;
    std::optional<rules::HandPattern> lastPattern;
    rules::PlayerId lastMovePlayer{PLAYER_HUMAN};
    rules::PlayerId currentPlayer{PLAYER_HUMAN};
    int passCount{0};
};

struct TurnDecisionTrace {
    std::string reasoningContent;
    std::string errorMessage;
};

struct TurnRecord {
    int turnNo{0};
    rules::PlayerId actor{PLAYER_HUMAN};
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
    StrategyMetadata strategy;
    TurnDecisionTrace trace;
};

std::string PlayerLabel(rules::PlayerId player);
std::string SourceLabel(TurnDecisionSource source);
std::string ReasonLabel(TurnDecisionReason reason);

} // namespace pdk::game
