#pragma once

#include "rules/Card.h"

#include <random>

namespace pdk::rules {

Cards CreatePaoDeKuaiDeck();
void Shuffle(Cards& deck, std::mt19937& rng);
int FindFirstPlayerBySpadeThree(const std::vector<Cards>& hands);

} // namespace pdk::rules
