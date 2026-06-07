#include "rules/MoveValidator.h"

namespace pdk::rules {

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

} // namespace pdk::rules
