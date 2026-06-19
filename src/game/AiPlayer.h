#pragma once

#include "game/AiStrategy.h"

#include <memory>
#include <utility>

namespace pdk::game {

class AiPlayer {
public:
    AiPlayer() : strategy_(std::make_unique<BasicAiStrategy>()) {}

    AiMoveChoice ChooseMove(const rules::Cards& hand, const AiContext& context) {
        return strategy_->ChooseMove(hand, context);
    }

    void SetStrategy(std::unique_ptr<AiStrategy> strategy) {
        strategy_ = strategy ? std::move(strategy) : std::make_unique<BasicAiStrategy>();
    }

private:
    std::unique_ptr<AiStrategy> strategy_;
};

} // namespace pdk::game
