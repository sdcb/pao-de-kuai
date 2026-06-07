#pragma once

#include "rules/Card.h"

#include <string>

namespace pdk::game {

struct PlayerState {
    std::string name;
    rules::Cards hand;
    bool hasPlayedCards{false};

    bool Empty() const { return hand.empty(); }
};

} // namespace pdk::game
