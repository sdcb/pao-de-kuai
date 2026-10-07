#pragma once

#include "rules/CppCompat.h"

#include <string>

namespace pdk::game {

struct PlayerState {
    std::string name;
    rules::Cards hand;
    bool hasPlayedCards{false};

    bool Empty() const { return hand.empty(); }
};

} // namespace pdk::game
