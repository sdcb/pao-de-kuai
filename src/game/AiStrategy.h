#pragma once

#include "game/CppCompat.h"
#include "game/StrategyMetadata.h"
#include "rules/CppCompat.h"

#include <array>
#include <optional>
#include <string>
#include <vector>

namespace pdk::game {

struct AiMoveChoice {
    bool pass{true};
    rules::Cards cards;
    rules::HandPattern pattern;
    std::string reason;
    // Strategy metadata for flavor text; gameplay validation still uses cards/pattern.
    int disruptionPenalty{0};
};

struct PassObservation {
    // Reliable because this game enforces "must play if you can beat it";
    // a legal pass proves the player could not beat this pattern at that time.
    rules::HandPattern pattern;
    int remainingCards{0};
};

struct AiContext {
    bool leading{true};
    rules::HandPattern previous;
    int ownRemainingCards{0};
    int currentPlayerIndex{0};
    int lastMovePlayerIndex{0};
    int trickLeaderIndex{0};
    int roundLeaderIndex{0};
    int currentTrickPassCount{0};
    int nextPlayerRemainingCards{0};
    int minOpponentRemainingCards{0};
    std::array<int, 3> remainingCards{0, 0, 0};
    rules::Cards playedCards;
    std::array<std::optional<PassObservation>, 3> passObservations{};
    std::array<std::vector<PassObservation>, 3> passHistory{};
};

class AiStrategy {
public:
    virtual ~AiStrategy() = default;
    virtual AiMoveChoice ChooseMove(const rules::Cards& hand, const AiContext& context) = 0;
    virtual StrategyMetadata Metadata() const { return {"unknown", "unknown"}; }
};

class BasicAiStrategy final : public AiStrategy {
public:
    AiMoveChoice ChooseMove(const rules::Cards& hand, const AiContext& context) override;
    StrategyMetadata Metadata() const override { return BasicStrategyMetadata(); }
    std::vector<AiMoveChoice> RecommendMoves(const rules::Cards& hand, const AiContext& context, int limit = 3) const;
};

class StrongAiStrategy final : public AiStrategy {
public:
    AiMoveChoice ChooseMove(const rules::Cards& hand, const AiContext& context) override;
    StrategyMetadata Metadata() const override { return StrongStrategyMetadata(); }
};

} // namespace pdk::game
