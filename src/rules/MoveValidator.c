#include "rules/MoveValidator.h"

#include <string.h>

static int CountMaskBits(uint64_t mask)
{
    int count = 0;
    while (mask != 0) {
        mask &= mask - 1;
        ++count;
    }
    return count;
}

static bool PossibleFollowCardCount(int selectedCount, const HandPattern *previous)
{
    if (selectedCount == 4) {
        return true;
    }
    if (previous->type == PATTERN_PLANE) {
        const int minCards = previous->groupCount * 4;
        const int maxCards = previous->groupCount * 5;
        return selectedCount >= minCards && selectedCount <= maxCards;
    }
    return selectedCount == previous->cardCount;
}

static void SetReason(char *dst, const char *reason)
{
    if (reason == NULL) {
        dst[0] = '\0';
        return;
    }
    strncpy(dst, reason, PATTERN_REASON_CAP - 1);
    dst[PATTERN_REASON_CAP - 1] = '\0';
}

MoveValidation ValidateLead(const Cards *cards, int handSizeBeforePlay)
{
    const PatternResult result = IdentifyPattern(cards, handSizeBeforePlay, true);
    MoveValidation validation;

    memset(&validation, 0, sizeof(validation));
    validation.pattern = result.pattern;
    if (!HandPattern_IsValid(&result.pattern)) {
        SetReason(validation.reason, result.reason);
        return validation;
    }
    validation.ok = true;
    return validation;
}

bool CanBeat(const HandPattern *candidate, const HandPattern *previous)
{
    if (!HandPattern_IsValid(candidate) || !HandPattern_IsValid(previous)) {
        return false;
    }
    if (candidate->type == PATTERN_BOMB && previous->type != PATTERN_BOMB) {
        return true;
    }
    if (candidate->type != PATTERN_BOMB && previous->type == PATTERN_BOMB) {
        return false;
    }
    if (!SameComparisonClass(candidate, previous)) {
        return false;
    }
    return RankValue(candidate->mainRank) > RankValue(previous->mainRank);
}

MoveValidation ValidateFollow(const Cards *cards, const HandPattern *previous,
                              int handSizeBeforePlay)
{
    const PatternResult result = IdentifyPattern(cards, handSizeBeforePlay, false);
    MoveValidation validation;

    memset(&validation, 0, sizeof(validation));
    validation.pattern = result.pattern;
    if (!HandPattern_IsValid(&result.pattern)) {
        SetReason(validation.reason, result.reason);
        return validation;
    }
    if (!CanBeat(&result.pattern, previous)) {
        SetReason(validation.reason, "牌型或点数压不过上家");
        return validation;
    }
    validation.ok = true;
    return validation;
}

bool HasAnyFollowMove(const Cards *hand, const HandPattern *previous, int handSizeBeforePlay)
{
    const int n = hand->count;
    int sourceHandSize;
    uint64_t limit;

    if (!HandPattern_IsValid(previous) || n == 0) {
        return false;
    }

    /* This is a rule-only existence check for UI/pass availability. It must not
     * use AI move ordering or scoring, otherwise button state and hover handling
     * can inherit AI strategy cost and behavior. */
    if (n >= 63) {
        return false;
    }

    /* Pao De Kuai hands are small enough here; exhaustive subsets keep the answer
     * exactly aligned with ValidateFollow without duplicating pattern rules. */
    sourceHandSize = handSizeBeforePlay >= 0 ? handSizeBeforePlay : n;
    limit = 1ull << n;
    for (uint64_t mask = 1; mask < limit; ++mask) {
        Cards cards;
        bool ok;

        if (!PossibleFollowCardCount(CountMaskBits(mask), previous)) {
            continue;
        }

        Cards_Clear(&cards);
        for (int i = 0; i < n; ++i) {
            if ((mask & (1ull << i)) != 0) {
                Cards_Push(&cards, hand->items[i]);
            }
        }
        ok = ValidateFollow(&cards, previous, sourceHandSize).ok;
        if (ok) {
            return true;
        }
    }
    return false;
}
