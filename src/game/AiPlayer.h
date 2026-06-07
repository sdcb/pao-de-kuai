#pragma once

#include "game/AiStrategy.h"

#include <memory>

namespace pdk::game {

class AiPlayer {
public:
    AiPlayer() : strategy_(std::make_unique<BasicAiStrategy>()) {}

    AiMoveChoice ChooseMove(const rules::Cards& hand, const AiContext& context) {
        return strategy_->ChooseMove(hand, context);
    }

private:
    std::unique_ptr<AiStrategy> strategy_;
};

} // namespace pdk::game
