#pragma once

#include "rules/Card.h"

namespace pdk::rules {

Cards CreatePaoDeKuaiDeck();
void Shuffle(Cards& deck, unsigned seed);
int FindFirstPlayerBySpadeThree(const std::vector<Cards>& hands);

} // namespace pdk::rules
