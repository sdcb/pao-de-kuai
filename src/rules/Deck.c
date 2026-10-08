#include "rules/Deck.h"

#include <stdlib.h>

Cards CreatePaoDeKuaiDeck(void)
{
    static const Suit suits[4] = {SUIT_SPADES, SUIT_HEARTS, SUIT_DIAMONDS, SUIT_CLUBS};
    static const Rank ranks[13] = {
        RANK_THREE, RANK_FOUR, RANK_FIVE, RANK_SIX, RANK_SEVEN, RANK_EIGHT,
        RANK_NINE, RANK_TEN, RANK_JACK, RANK_QUEEN, RANK_KING, RANK_ACE, RANK_TWO
    };
    Cards deck;

    Cards_Clear(&deck);
    for (int r = 0; r < 13; ++r) {
        for (int s = 0; s < 4; ++s) {
            Card card;
            card.rank = ranks[r];
            card.suit = suits[s];
            if (card.rank == RANK_TWO && card.suit != SUIT_SPADES) {
                continue;
            }
            if (card.rank == RANK_ACE && card.suit == SUIT_CLUBS) {
                continue;
            }
            Cards_Push(&deck, card);
        }
    }
    return deck;
}

void Shuffle(Cards *deck, unsigned seed)
{
    srand(seed);
    for (int i = deck->count; i > 1; --i) {
        const int j = (int)((unsigned)rand() % (unsigned)i);
        const Card swap = deck->items[i - 1];
        deck->items[i - 1] = deck->items[j];
        deck->items[j] = swap;
    }
}

int FindFirstPlayerBySpadeThree(const Cards *hands, int handCount)
{
    for (int player = 0; player < handCount; ++player) {
        for (int i = 0; i < hands[player].count; ++i) {
            if (IsSpadeThree(hands[player].items[i])) {
                return player;
            }
        }
    }
    return 0;
}
