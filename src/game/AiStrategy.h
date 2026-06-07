#pragma once

#include "rules/MoveValidator.h"

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
};

struct AiContext {
    bool leading{true};
    rules::HandPattern previous;
    int ownRemainingCards{0};
    std::array<int, 3> remainingCards{0, 0, 0};
};

class AiStrategy {
public:
    virtual ~AiStrategy() = default;
    virtual AiMoveChoice ChooseMove(const rules::Cards& hand, const AiContext& context) = 0;
};

class BasicAiStrategy final : public AiStrategy {
public:
    AiMoveChoice ChooseMove(const rules::Cards& hand, const AiContext& context) override;
};

} // namespace pdk::game
