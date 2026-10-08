#include "rules/Card.h"

#include <string.h>

void Cards_Clear(Cards *cards)
{
    cards->count = 0;
}

bool Cards_Push(Cards *cards, Card card)
{
    if (cards->count >= CARDS_MAX) {
        return false;
    }
    cards->items[cards->count++] = card;
    return true;
}

bool Cards_Remove(Cards *cards, Card card)
{
    const int index = Cards_IndexOf(cards, card);
    if (index < 0) {
        return false;
    }
    Cards_RemoveAt(cards, index);
    return true;
}

void Cards_RemoveAt(Cards *cards, int index)
{
    if (index < 0 || index >= cards->count) {
        return;
    }
    for (int i = index; i + 1 < cards->count; ++i) {
        cards->items[i] = cards->items[i + 1];
    }
    --cards->count;
}

bool Cards_Contains(const Cards *cards, Card card)
{
    return Cards_IndexOf(cards, card) >= 0;
}

int Cards_IndexOf(const Cards *cards, Card card)
{
    for (int i = 0; i < cards->count; ++i) {
        if (cards->items[i].rank == card.rank && cards->items[i].suit == card.suit) {
            return i;
        }
    }
    return -1;
}

bool Cards_Append(Cards *dst, const Cards *src)
{
    for (int i = 0; i < src->count; ++i) {
        if (!Cards_Push(dst, src->items[i])) {
            return false;
        }
    }
    return true;
}

/* Same order as the defaulted operator<=> on (rank, suit). */
int Cards_Compare(const Cards *lhs, const Cards *rhs)
{
    const int shared = lhs->count < rhs->count ? lhs->count : rhs->count;
    for (int i = 0; i < shared; ++i) {
        const Card a = lhs->items[i];
        const Card b = rhs->items[i];
        if (a.rank != b.rank) {
            return a.rank < b.rank ? -1 : 1;
        }
        if (a.suit != b.suit) {
            return a.suit < b.suit ? -1 : 1;
        }
    }
    if (lhs->count != rhs->count) {
        return lhs->count < rhs->count ? -1 : 1;
    }
    return 0;
}

int RankValue(Rank rank)
{
    return (int)rank;
}

int SortValue(Card card)
{
    return RankValue(card.rank) * 10 + (int)card.suit;
}

bool IsSpadeThree(Card card)
{
    return card.rank == RANK_THREE && card.suit == SUIT_SPADES;
}

const char *RankName(Rank rank)
{
    switch (rank) {
    case RANK_THREE: return "3";
    case RANK_FOUR: return "4";
    case RANK_FIVE: return "5";
    case RANK_SIX: return "6";
    case RANK_SEVEN: return "7";
    case RANK_EIGHT: return "8";
    case RANK_NINE: return "9";
    case RANK_TEN: return "10";
    case RANK_JACK: return "J";
    case RANK_QUEEN: return "Q";
    case RANK_KING: return "K";
    case RANK_ACE: return "A";
    case RANK_TWO: return "2";
    }
    return "?";
}

const char *SuitName(Suit suit)
{
    switch (suit) {
    case SUIT_SPADES: return "S";
    case SUIT_HEARTS: return "H";
    case SUIT_DIAMONDS: return "D";
    case SUIT_CLUBS: return "C";
    }
    return "?";
}

/* Appends while there is room, so a truncated string is still terminated. */
static void AppendText(char *out, int cap, int *used, const char *text)
{
    while (*text != '\0' && *used + 1 < cap) {
        out[(*used)++] = *text++;
    }
    out[*used] = '\0';
}

void Card_ToString(Card card, char *out, int cap)
{
    int used = 0;

    if (out == NULL || cap <= 0) {
        return;
    }
    out[0] = '\0';
    AppendText(out, cap, &used, RankName(card.rank));
    AppendText(out, cap, &used, SuitName(card.suit));
}

void Cards_ToString(const Cards *cards, char *out, int cap)
{
    int used = 0;

    if (out == NULL || cap <= 0) {
        return;
    }
    out[0] = '\0';
    for (int i = 0; i < cards->count; ++i) {
        char single[8];
        if (i != 0) {
            AppendText(out, cap, &used, " ");
        }
        Card_ToString(cards->items[i], single, (int)sizeof(single));
        AppendText(out, cap, &used, single);
    }
}

void SortByGameOrder(Cards *cards)
{
    /* Insertion sort: hands are at most 16 cards and the deck is only sorted
     * once per round, so this beats pulling in qsort and its callback. */
    for (int i = 1; i < cards->count; ++i) {
        const Card key = cards->items[i];
        const int keyValue = SortValue(key);
        int j = i - 1;
        while (j >= 0 && SortValue(cards->items[j]) > keyValue) {
            cards->items[j + 1] = cards->items[j];
            --j;
        }
        cards->items[j + 1] = key;
    }
}
