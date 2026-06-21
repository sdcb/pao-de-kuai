#include "rules/MoveValidator.h"

#include <cstdint>

namespace pdk::rules {
namespace {

int CountMaskBits(std::uint64_t mask) {
    int count = 0;
    while (mask != 0) {
        mask &= mask - 1;
        count++;
    }
    return count;
}

bool PossibleFollowCardCount(int selectedCount, const HandPattern& previous) {
    if (selectedCount == 4) {
        return true;
    }
    if (previous.type == PatternType::Plane) {
        const int minCards = previous.groupCount * 4;
        const int maxCards = previous.groupCount * 5;
        return selectedCount >= minCards && selectedCount <= maxCards;
    }
    return selectedCount == previous.cardCount;
}

} // namespace

MoveValidation ValidateLead(const Cards& cards, int handSizeBeforePlay) {
    const auto result = IdentifyPattern(cards, handSizeBeforePlay, true);
    if (!result.pattern.IsValid()) {
        return MoveValidation{false, result.pattern, result.reason};
    }
    return MoveValidation{true, result.pattern, {}};
}

bool CanBeat(const HandPattern& candidate, const HandPattern& previous) {
    if (!candidate.IsValid() || !previous.IsValid()) {
        return false;
    }
    if (candidate.type == PatternType::Bomb && previous.type != PatternType::Bomb) {
        return true;
    }
    if (candidate.type != PatternType::Bomb && previous.type == PatternType::Bomb) {
        return false;
    }
    if (!SameComparisonClass(candidate, previous)) {
        return false;
    }
    return RankValue(candidate.mainRank) > RankValue(previous.mainRank);
}

MoveValidation ValidateFollow(const Cards& cards, const HandPattern& previous, int handSizeBeforePlay) {
    const auto result = IdentifyPattern(cards, handSizeBeforePlay, false);
    if (!result.pattern.IsValid()) {
        return MoveValidation{false, result.pattern, result.reason};
    }
    if (!CanBeat(result.pattern, previous)) {
        return MoveValidation{false, result.pattern, "牌型或点数压不过上家"};
    }
    return MoveValidation{true, result.pattern, {}};
}

bool HasAnyFollowMove(const Cards& hand, const HandPattern& previous, int handSizeBeforePlay) {
    if (!previous.IsValid() || hand.empty()) {
        return false;
    }

    // This is a rule-only existence check for UI/pass availability. It must not
    // use AI move ordering or scoring, otherwise button state and hover handling
    // can inherit AI strategy cost and behavior.
    const int n = static_cast<int>(hand.size());
    if (n >= 63) {
        return false;
    }

    // Pao De Kuai hands are small enough here; exhaustive subsets keep the answer
    // exactly aligned with ValidateFollow without duplicating pattern rules.
    const int sourceHandSize = handSizeBeforePlay >= 0 ? handSizeBeforePlay : n;
    const std::uint64_t limit = 1ull << n;
    for (std::uint64_t mask = 1; mask < limit; ++mask) {
        const int selectedCount = CountMaskBits(mask);
        if (!PossibleFollowCardCount(selectedCount, previous)) {
            continue;
        }

        Cards cards;
        cards.reserve(n);
        for (int i = 0; i < n; ++i) {
            if ((mask & (1ull << i)) != 0) {
                cards.push_back(hand[static_cast<std::size_t>(i)]);
            }
        }
        if (ValidateFollow(cards, previous, sourceHandSize).ok) {
            return true;
        }
    }
    return false;
}

} // namespace pdk::rules
