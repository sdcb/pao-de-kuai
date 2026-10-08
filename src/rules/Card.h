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
 *
 * TEMPORARY: the `#ifdef __cplusplus` block inside `Cards` gives the not-yet-ported
 * C++ translation units the std::vector-shaped API they still use, so the container
 * could flip to the C type in one step instead of leaving the tree broken while
 * ~250 call sites were rewritten.  It is deleted together with
 * src/rules/CppCompat.h, and `tools/check_c_only.py` whitelists this one header.
 * A file that has been converted to C loses these members automatically.
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

/* The tag is completed after the function declarations so that the temporary C++
 * member shim below can call into the C API. */
typedef struct Cards Cards;

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

struct Cards {
    Card items[CARDS_MAX];
    int count;

#ifdef __cplusplus
    /* ---- C++ member shim for the test suite (stays; see tools/check_c_only.py) ---- */
    /*
     * src/ is pure C now, but tests/ is permanently C++ (doctest) and uses the vector-shaped API
     * below in ~139 places, so this block is not a leftover: it is test support that lives here
     * because a member cannot be added to the struct from outside.  It is the one documented
     * exception in tools/check_c_only.py's whitelist.
     *
     * A zeroing default constructor is not optional: `Cards x;` used to be an empty
     * std::vector, and without this the C struct would leave `count` indeterminate
     * and the first push_back would write out of bounds.  C callers use
     * Cards_Clear instead.
     */
    Cards() : count(0) {}
    Cards(const Card *first, const Card *last) : count(0)
    {
        for (const Card *p = first; p != last; ++p) {
            Cards_Push(this, *p);
        }
    }
    int size() const { return count; }
    bool empty() const { return count == 0; }
    void clear() { count = 0; }
    void reserve(int) {}
    void resize(int n)
    {
        while (count < n) {
            Card zero;
            zero.rank = 0;
            zero.suit = 0;
            items[count++] = zero;
        }
        if (n >= 0) {
            count = n;
        }
    }
    void push_back(Card card) { Cards_Push(this, card); }
    void emplace_back(Card card) { Cards_Push(this, card); }
    void pop_back()
    {
        if (count > 0) {
            --count;
        }
    }
    Card &operator[](int index) { return items[index]; }
    const Card &operator[](int index) const { return items[index]; }
    Card &front() { return items[0]; }
    const Card &front() const { return items[0]; }
    Card &back() { return items[count - 1]; }
    const Card &back() const { return items[count - 1]; }
    Card *begin() { return items; }
    const Card *begin() const { return items; }
    Card *end() { return items + count; }
    const Card *end() const { return items + count; }
    Card *erase(Card *position)
    {
        const int index = static_cast<int>(position - items);
        Cards_RemoveAt(this, index);
        return items + index;
    }
    Card *erase(Card *first, Card *last)
    {
        const int from = static_cast<int>(first - items);
        const int to = static_cast<int>(last - items);
        for (int i = to; i < count; ++i) {
            items[from + (i - to)] = items[i];
        }
        count -= to - from;
        return items + from;
    }
    Card *insert(Card *position, Card card)
    {
        const int index = static_cast<int>(position - items);
        if (count < CARDS_MAX) {
            for (int i = count; i > index; --i) {
                items[i] = items[i - 1];
            }
            items[index] = card;
            ++count;
        }
        return items + index;
    }
    Card *insert(Card *position, const Card *first, const Card *last)
    {
        const int index = static_cast<int>(position - items);
        const int added = static_cast<int>(last - first);
        if (added > 0 && count + added <= CARDS_MAX) {
            for (int i = count + added - 1; i >= index + added; --i) {
                items[i] = items[i - added];
            }
            for (int i = 0; i < added; ++i) {
                items[index + i] = first[i];
            }
            count += added;
        }
        return items + index;
    }
#endif
};

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
