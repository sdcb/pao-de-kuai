#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * `src/rules/` is pure C (plan.md S2) and `Cards` is now the fixed-capacity C
 * value type everywhere, not just inside src/rules.  That was the plan's route A
 * decision (appendix B): keeping a std::vector facade for the container would have
 * meant rewriting every call site twice, so the container flipped once here and the
 * remaining C++ translation units were converted to the C API in the same step.
 *
 * What is left is a pure name bridge: every rules symbol already lives at global
 * scope, and the C++ callers spell them `rules::X`.  The reference-shaped inline
 * overloads exist only so those call sites keep compiling; they all forward to the
 * pointer-shaped C API.
 *
 * It must not grow new features, and it disappears together with the last C++ file
 * under src/.
 */

#include <algorithm>
#include <array>
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

/*
 * Builds a Cards from a braced list.  A bare `Cards{...}` would initialise only the
 * items array and leave count at zero, so every aggregate-construction site goes
 * through this instead; TestHelpers.h wraps it as MakeCards.
 */
inline Cards MakeCards(std::initializer_list<Card> cards)
{
    Cards out;
    Cards_Clear(&out);
    for (const Card& card : cards) {
        Cards_Push(&out, card);
    }
    return out;
}

inline Cards MakeCards(const std::vector<Card>& cards)
{
    Cards out;
    Cards_Clear(&out);
    for (const Card& card : cards) {
        Cards_Push(&out, card);
    }
    return out;
}

namespace pdk::rules {

using ::BombScoreEvent;
using ::Card;
using ::Cards;
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

using ::FixedPaoDeKuaiRuleSet;
using ::IsSpadeThree;
using ::MakeCards;
using ::PlayerFromIndex;
using ::PlayerIndex;
using ::RankValue;
using ::SortValue;

/* These six used to return std::string / std::string_view, and callers still
 * write `.c_str()` or `"prefix" + PlayerKey(x)`, so the facade returns
 * std::string. */
inline std::string PlayerKey(PlayerId player) { return std::string(::PlayerKey(player)); }
inline std::string RankName(Rank rank) { return std::string(::RankName(rank)); }
inline std::string SuitName(Suit suit) { return std::string(::SuitName(suit)); }
inline std::string PatternName(PatternType type) { return std::string(::PatternName(type)); }
inline std::string SharedGameRulesText() { return std::string(::SharedGameRulesText()); }
inline std::string HumanHelpText() { return std::string(::HumanHelpText()); }

/* Reference-shaped overloads of the pointer-shaped C API. */
inline bool IsValid(const HandPattern& pattern)
{
    return ::HandPattern_IsValid(&pattern);
}

inline bool CanBeat(const HandPattern& candidate, const HandPattern& previous)
{
    return ::CanBeat(&candidate, &previous);
}

inline bool SameComparisonClass(const HandPattern& lhs, const HandPattern& rhs)
{
    return ::SameComparisonClass(&lhs, &rhs);
}

inline std::string PatternDescription(const HandPattern& pattern)
{
    char buffer[PATTERN_REASON_CAP];
    ::PatternDescription(&pattern, buffer, static_cast<int>(sizeof(buffer)));
    return std::string(buffer);
}

inline std::string ToString(Card card)
{
    char buffer[8];
    Card_ToString(card, buffer, static_cast<int>(sizeof(buffer)));
    return std::string(buffer);
}

inline std::string ToString(const Cards& cards)
{
    char buffer[256];
    Cards_ToString(&cards, buffer, static_cast<int>(sizeof(buffer)));
    return std::string(buffer);
}

inline PatternResult IdentifyPattern(const Cards& cards,
                                     int handSizeBeforePlay = -1,
                                     bool allowShortFinal = false)
{
    return ::IdentifyPattern(&cards, handSizeBeforePlay, allowShortFinal);
}

inline MoveValidation ValidateLead(const Cards& cards, int handSizeBeforePlay = -1)
{
    return ::ValidateLead(&cards, handSizeBeforePlay);
}

inline MoveValidation ValidateFollow(const Cards& cards,
                                    const HandPattern& previous,
                                    int handSizeBeforePlay = -1)
{
    return ::ValidateFollow(&cards, &previous, handSizeBeforePlay);
}

inline bool HasAnyFollowMove(const Cards& hand,
                            const HandPattern& previous,
                            int handSizeBeforePlay = -1)
{
    return ::HasAnyFollowMove(&hand, &previous, handSizeBeforePlay);
}

inline RoundScoreResult CalculateRoundScore(const RoundScoreInput& input)
{
    return ::CalculateRoundScore(&input);
}

inline Cards CreatePaoDeKuaiDeck()
{
    return ::CreatePaoDeKuaiDeck();
}

inline void Shuffle(Cards& deck, unsigned seed)
{
    ::Shuffle(&deck, seed);
}

inline void SortByGameOrder(Cards& cards)
{
    ::SortByGameOrder(&cards);
}

/* Accepts std::vector<Cards> and std::array<Cards, N> alike. */
template <typename HandContainer>
inline int FindFirstPlayerBySpadeThree(const HandContainer& hands)
{
    std::vector<Cards> copy(hands.begin(), hands.end());
    return ::FindFirstPlayerBySpadeThree(copy.data(), static_cast<int>(copy.size()));
}

/* ---- the rule book --------------------------------------------------- */

class PaoDeKuaiRules {
public:
    PaoDeKuaiRules() : data_(::PaoDeKuaiRules_Create()) {}

    const RuleSet& Rule() const { return data_.rule; }

    Cards CreateDeck() const { return ::PaoDeKuaiRules_CreateDeck(&data_); }

    MoveValidation ValidateLeadMove(const Cards& cards, int handSizeBeforePlay = -1) const
    {
        return ::PaoDeKuaiRules_ValidateLeadMove(&data_, &cards, handSizeBeforePlay);
    }

    MoveValidation ValidateFollowMove(const Cards& cards,
                                     const HandPattern& previous,
                                     int handSizeBeforePlay = -1) const
    {
        return ::PaoDeKuaiRules_ValidateFollowMove(&data_, &cards, &previous, handSizeBeforePlay);
    }

private:
    ::PaoDeKuaiRules data_;
};

} // namespace pdk::rules
