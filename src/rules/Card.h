#pragma once

/*
 * Cards, ranks and suits.
 *
 * Pure C.  Two deliberate choices worth knowing about (plan.md 2):
 *
 *  1. `Rank`/`Suit`/`PatternType`/`PlayerId` are `uint8_t` typedefs with
 *     anonymous enum constants, not `enum` types.  In C an enum is `int`, which
 *     would make `Card` 8 bytes and `Cards` 388 bytes; the AI search copies
 *     hands in hot loops, so the compact layout matters.  The constants still
 *     read like an enum (`RANK_THREE`, `SUIT_SPADES`), which is the naming the
 *     porting conventions prescribe.
 *
 *  2. `Cards` is a fixed-capacity value type instead of `std::vector<Card>`.
 *     The biggest set in the project is the 48-card deck and the biggest hand is
 *     16, so this removes every heap allocation from the rules layer and makes
 *     the type trivially copyable.  All writes go through `Cards_Push`, which
 *     clamps at `CARDS_MAX` rather than overflowing.
 */

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef uint8_t Suit;
typedef uint8_t Rank;
typedef uint8_t PatternType;
typedef uint8_t PlayerId;

enum {
    SUIT_SPADES = 0,
    SUIT_HEARTS = 1,
    SUIT_DIAMONDS = 2,
    SUIT_CLUBS = 3
};

enum {
    RANK_THREE = 3,
    RANK_FOUR = 4,
    RANK_FIVE = 5,
    RANK_SIX = 6,
    RANK_SEVEN = 7,
    RANK_EIGHT = 8,
    RANK_NINE = 9,
    RANK_TEN = 10,
    RANK_JACK = 11,
    RANK_QUEEN = 12,
    RANK_KING = 13,
    RANK_ACE = 14,
    RANK_TWO = 15
};

typedef struct Card {
    Rank rank;
    Suit suit;
} Card;

enum { CARDS_MAX = 48 };

typedef struct Cards {
    Card items[CARDS_MAX];
    int count;
} Cards;

void Cards_Clear(Cards *cards);
/* Returns false when the array is full; the card is dropped, never overflowing. */
bool Cards_Push(Cards *cards, Card card);
bool Cards_Remove(Cards *cards, Card card);
void Cards_RemoveAt(Cards *cards, int index);
bool Cards_Contains(const Cards *cards, Card card);
int Cards_IndexOf(const Cards *cards, Card card);
bool Cards_Append(Cards *dst, const Cards *src);
/* Lexicographic (rank, then suit), matching the old defaulted operator<=>. */
int Cards_Compare(const Cards *lhs, const Cards *rhs);

int RankValue(Rank rank);
int SortValue(Card card);
bool IsSpadeThree(Card card);

/* All four return a pointer to a string literal, so there is nothing to free. */
const char *RankName(Rank rank);
const char *SuitName(Suit suit);
/* Writes at most `cap` bytes including the terminator; always NUL terminates. */
void Card_ToString(Card card, char *out, int cap);
void Cards_ToString(const Cards *cards, char *out, int cap);
void SortByGameOrder(Cards *cards);

#ifdef __cplusplus
}
#endif
