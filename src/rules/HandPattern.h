#pragma once

#include "rules/Card.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PATTERN_INVALID = 0,
    PATTERN_SINGLE = 1,
    PATTERN_PAIR = 2,
    PATTERN_STRAIGHT = 3,
    PATTERN_CONSECUTIVE_PAIRS = 4,
    PATTERN_TRIPLE_WITH_ONE = 5,
    PATTERN_TRIPLE_WITH_PAIR = 6,
    PATTERN_PLANE = 7,
    PATTERN_BOMB = 8
};

typedef struct HandPattern {
    PatternType type;
    Rank mainRank;
    int cardCount;
    int groupCount;
    bool lastHandShort;
} HandPattern;

bool HandPattern_IsValid(const HandPattern *pattern);

/* `reason` is a fixed buffer rather than a string: the longest message in this
 * file is 24 bytes and hand patterns are produced inside the AI search hot path,
 * so a heap allocation there would be pure waste (plan.md 2). */
enum { PATTERN_REASON_CAP = 128 };

typedef struct PatternResult {
    HandPattern pattern;
    char reason[PATTERN_REASON_CAP];
} PatternResult;

/* `handSizeBeforePlay` is the hand size before the play, used to recognise the
 * "last hand is short of kickers" special case; -1 means unknown.
 * `allowShortFinal` must only be true when validating a lead (AGENTS.md). */
PatternResult IdentifyPattern(const Cards *cards, int handSizeBeforePlay, bool allowShortFinal);

/* Returns a string literal. */
const char *PatternName(PatternType type);
void PatternDescription(const HandPattern *pattern, char *out, int cap);
bool SameComparisonClass(const HandPattern *lhs, const HandPattern *rhs);

#ifdef __cplusplus
}
#endif
