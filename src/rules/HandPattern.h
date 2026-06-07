#pragma once

#include "rules/Card.h"

#include <optional>
#include <string>

namespace pdk::rules {

enum class PatternType {
    Invalid,
    Single,
    Pair,
    Straight,
    ConsecutivePairs,
    TripleWithOne,
    TripleWithPair,
    Plane,
    Bomb
};

struct HandPattern {
    PatternType type{PatternType::Invalid};
    Rank mainRank{Rank::Three};
    int cardCount{0};
    int groupCount{0};
    bool lastHandShort{false};

    bool IsValid() const { return type != PatternType::Invalid; }
};

struct PatternResult {
    HandPattern pattern{};
    std::string reason;
};

PatternResult IdentifyPattern(const Cards& cards, int handSizeBeforePlay = -1, bool allowShortFinalPlane = false);
std::string PatternName(PatternType type);
std::string PatternDescription(const HandPattern& pattern);
bool SameComparisonClass(const HandPattern& lhs, const HandPattern& rhs);

} // namespace pdk::rules
