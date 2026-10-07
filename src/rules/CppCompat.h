#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * `src/rules/` is pure C now (plan.md S2): the C API takes `const Cards*` and
 * returns fixed-size structs.  The C++ translation units that have not been
 * converted yet still spell the old API, and they spell *every* rules symbol as
 * `rules::X` (verified: there is not a single unqualified rules reference outside
 * src/rules), so this header can present the same names inside
 * `namespace pdk::rules` and nothing else has to change.
 *
 * Two things it does:
 *   1. `Cards` stays `std::vector<Card>` for the C++ callers, and each C API
 *      function gets a thin inline overload that packs the vector into the C
 *      fixed-capacity `Cards` for the call.  The pack is at most 48 * 2 bytes.
 *   2. `PaoDeKuaiRules` keeps its class shape.
 *
 * It must not grow new features, and it disappears together with the last C++
 * file under src/.  Files depending on it are every C++ file that includes a
 * rules header.
 */

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "rules/Card.h"
#include "rules/Deck.h"
#include "rules/HandPattern.h"
#include "rules/MoveValidator.h"
#include "rules/PaoDeKuaiRules.h"
#include "rules/RuleSet.h"
#include "rules/RuleText.h"
#include "rules/Scoring.h"

/* Card used to have a defaulted operator<=>.  These live at global scope so that
 * argument-dependent lookup finds them for `::Card`. */
inline bool operator==(Card lhs, Card rhs)
{
    return lhs.rank == rhs.rank && lhs.suit == rhs.suit;
}

inline bool operator!=(Card lhs, Card rhs)
{
    return !(lhs == rhs);
}

inline bool operator<(Card lhs, Card rhs)
{
    if (lhs.rank != rhs.rank) {
        return lhs.rank < rhs.rank;
    }
    return lhs.suit < rhs.suit;
}

inline bool operator>(Card lhs, Card rhs)
{
    return rhs < lhs;
}

inline bool operator<=(Card lhs, Card rhs)
{
    return !(rhs < lhs);
}

inline bool operator>=(Card lhs, Card rhs)
{
    return !(lhs < rhs);
}

namespace pdk::rules {

using ::BombScoreEvent;
using ::Card;
using ::HandPattern;
using ::MoveValidation;
using ::PatternResult;
using ::PatternType;
using ::PlayerId;
using ::Rank;
using ::RoundScoreInput;
using ::RoundScoreResult;
using ::RuleSet;
using ::SpringInfo;
using ::Suit;

/*
 * Functions whose C signature the C++ callers can use unchanged.  Pulling them
 * into the namespace keeps `rules::Foo(...)` working even though `Foo` now lives
 * at global scope.  `CreatePaoDeKuaiDeck` is deliberately absent: its C form
 * returns the fixed-capacity struct and cannot coexist with the vector-returning
 * wrapper below.
 */
using ::FixedPaoDeKuaiRuleSet;
using ::IsSpadeThree;
using ::PlayerFromIndex;
using ::PlayerIndex;
using ::RankValue;
using ::SortValue;

/* These six used to return std::string / std::string_view, and callers still
 * write `.c_str()` or `"prefix" + PlayerKey(x)`, so the facade returns
 * std::string.  That also keeps the string_view overload in HelpScene safe: the
 * temporary lives until the end of the full expression that consumes it. */
inline std::string PlayerKey(PlayerId player) { return std::string(::PlayerKey(player)); }
inline std::string RankName(Rank rank) { return std::string(::RankName(rank)); }
inline std::string SuitName(Suit suit) { return std::string(::SuitName(suit)); }
inline std::string PatternName(PatternType type) { return std::string(::PatternName(type)); }
inline std::string SharedGameRulesText() { return std::string(::SharedGameRulesText()); }
inline std::string HumanHelpText() { return std::string(::HumanHelpText()); }

/* Overloaded rather than imported: the C forms take pointers and these take
 * references, so both stay callable and the reference form wins for C++ callers. */
using ::CalculateRoundScore;
using ::CanBeat;
using ::HasAnyFollowMove;
using ::IdentifyPattern;
using ::PatternDescription;
using ::SameComparisonClass;
using ::Shuffle;
using ::SortByGameOrder;
using ::ValidateFollow;
using ::ValidateLead;

/* `Cards` deliberately shadows the C fixed-capacity struct. */
using Cards = std::vector<Card>;

/* ---- vector <-> fixed array bridging --------------------------------- */

inline ::Cards ToCCards(const Cards& in)
{
    ::Cards out;
    Cards_Clear(&out);
    for (const Card& card : in) {
        Cards_Push(&out, card);
    }
    return out;
}

inline Cards FromCCards(const ::Cards& in)
{
    Cards out;
    out.reserve(static_cast<std::size_t>(in.count));
    for (int i = 0; i < in.count; ++i) {
        out.push_back(in.items[i]);
    }
    return out;
}

/* ---- functions whose C form takes a pointer -------------------------- */

inline int RankValueOf(Rank rank) { return ::RankValue(rank); }

inline bool CanBeat(const HandPattern& candidate, const HandPattern& previous)
{
    return ::CanBeat(&candidate, &previous);
}

inline bool SameComparisonClass(const HandPattern& lhs, const HandPattern& rhs)
{
    return ::SameComparisonClass(&lhs, &rhs);
}

/* Replaces the old `HandPattern::IsValid()` member call. */
inline bool IsValid(const HandPattern& pattern)
{
    return ::HandPattern_IsValid(&pattern);
}

inline std::string ToString(Card card)
{
    char buffer[8];
    Card_ToString(card, buffer, static_cast<int>(sizeof(buffer)));
    return std::string(buffer);
}

inline std::string ToString(const Cards& cards)
{
    ::Cards packed = ToCCards(cards);
    char buffer[256];
    Cards_ToString(&packed, buffer, static_cast<int>(sizeof(buffer)));
    return std::string(buffer);
}

inline PatternResult IdentifyPattern(const Cards& cards,
                                     int handSizeBeforePlay = -1,
                                     bool allowShortFinal = false)
{
    const ::Cards packed = ToCCards(cards);
    return ::IdentifyPattern(&packed, handSizeBeforePlay, allowShortFinal);
}

inline std::string PatternDescription(const HandPattern& pattern)
{
    char buffer[PATTERN_REASON_CAP];
    ::PatternDescription(&pattern, buffer, static_cast<int>(sizeof(buffer)));
    return std::string(buffer);
}

inline MoveValidation ValidateLead(const Cards& cards, int handSizeBeforePlay = -1)
{
    const ::Cards packed = ToCCards(cards);
    return ::ValidateLead(&packed, handSizeBeforePlay);
}

inline MoveValidation ValidateFollow(const Cards& cards,
                                    const HandPattern& previous,
                                    int handSizeBeforePlay = -1)
{
    const ::Cards packed = ToCCards(cards);
    return ::ValidateFollow(&packed, &previous, handSizeBeforePlay);
}

inline bool HasAnyFollowMove(const Cards& hand,
                            const HandPattern& previous,
                            int handSizeBeforePlay = -1)
{
    const ::Cards packed = ToCCards(hand);
    return ::HasAnyFollowMove(&packed, &previous, handSizeBeforePlay);
}

inline RoundScoreResult CalculateRoundScore(const RoundScoreInput& input)
{
    return ::CalculateRoundScore(&input);
}

/* ---- deck ------------------------------------------------------------ */

/* Return type differs from the C twin, so this one is not an overload of it. */
inline Cards CreatePaoDeKuaiDeck()
{
    return FromCCards(::CreatePaoDeKuaiDeck());
}

inline void Shuffle(Cards& deck, unsigned seed)
{
    ::Cards packed = ToCCards(deck);
    ::Shuffle(&packed, seed);
    deck = FromCCards(packed);
}

inline void SortByGameOrder(Cards& cards)
{
    std::sort(cards.begin(), cards.end(), [](Card lhs, Card rhs) {
        return SortValue(lhs) < SortValue(rhs);
    });
}

template <typename HandContainer>
inline int FindFirstPlayerBySpadeThree(const HandContainer& hands)
{
    std::vector<::Cards> packed;
    packed.reserve(hands.size());
    for (const Cards& hand : hands) {
        packed.push_back(ToCCards(hand));
    }
    return ::FindFirstPlayerBySpadeThree(packed.data(), static_cast<int>(packed.size()));
}

/* ---- the rule book --------------------------------------------------- */

class PaoDeKuaiRules {
public:
    PaoDeKuaiRules() : data_(::PaoDeKuaiRules_Create()) {}

    const RuleSet& Rule() const { return data_.rule; }

    Cards CreateDeck() const { return FromCCards(::PaoDeKuaiRules_CreateDeck(&data_)); }

    MoveValidation ValidateLeadMove(const Cards& cards, int handSizeBeforePlay = -1) const
    {
        const ::Cards packed = ToCCards(cards);
        return ::PaoDeKuaiRules_ValidateLeadMove(&data_, &packed, handSizeBeforePlay);
    }

    MoveValidation ValidateFollowMove(const Cards& cards,
                                     const HandPattern& previous,
                                     int handSizeBeforePlay = -1) const
    {
        const ::Cards packed = ToCCards(cards);
        return ::PaoDeKuaiRules_ValidateFollowMove(&data_, &packed, &previous, handSizeBeforePlay);
    }

private:
    ::PaoDeKuaiRules data_;
};

} // namespace pdk::rules
