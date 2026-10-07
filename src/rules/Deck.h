#pragma once

#include "rules/Card.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The fixed 48-card Pao De Kuai deck: no jokers, only the spade two, no club
 * ace (AGENTS.md "固定游戏规则"). */
Cards CreatePaoDeKuaiDeck(void);
/* Uses srand/rand so a fixed seed keeps producing the same deal as before; the
 * rules tests rely on that. */
void Shuffle(Cards *deck, unsigned seed);
/* `hands` is an array of `handCount` hands; returns the first player holding the
 * spade three, or 0 when nobody does. */
int FindFirstPlayerBySpadeThree(const Cards *hands, int handCount);

#ifdef __cplusplus
}
#endif
