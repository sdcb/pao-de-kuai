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

/* `dst` is already zeroed, so copying up to the NUL leaves the same bytes strncpy's
 * padding used to write. */
static void SetReason(char *dst, const char *reason)
{
    for (int i = 0; reason != NULL && reason[i] != '\0' && i + 1 < PATTERN_REASON_CAP; ++i) {
        dst[i] = reason[i];
    }
}

MoveValidation ValidateLead(const Cards *cards, int handSizeBeforePlay)
{
    MoveValidation validation;
    const char *reason = NULL;

    memset(&validation, 0, sizeof(validation));
    if (!IdentifyPatternOnly(cards, handSizeBeforePlay, true, &validation.pattern, &reason)) {
        SetReason(validation.reason, reason);
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
    MoveValidation validation;
    const char *reason = NULL;

    memset(&validation, 0, sizeof(validation));
    if (!IdentifyPatternOnly(cards, handSizeBeforePlay, false, &validation.pattern, &reason)) {
        SetReason(validation.reason, reason);
        return validation;
    }
    if (!CanBeat(&validation.pattern, previous)) {
        SetReason(validation.reason, "牌型或点数压不过上家");
        return validation;
    }
    validation.ok = true;
    return validation;
}

bool HasAnyFollowMoveExhaustive(const Cards *hand, const HandPattern *previous,
                                int handSizeBeforePlay)
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

/* True when `length` consecutive ranks, all below 2 and ending above `above`, each
 * have at least `minCount` cards. */
static bool HasRun(const int *count, int length, int minCount, int above)
{
    int run = 0;

    for (int rank = RANK_THREE; rank <= RANK_ACE; ++rank) {
        run = count[rank] >= minCount ? run + 1 : 0;
        if (run >= length && rank > above) {
            return true;
        }
    }
    return false;
}

/*
 * The same answer as HasAnyFollowMoveExhaustive, read off the rank counts instead of
 * trying all 2^n subsets.  The AI's opponent sampling asks this question hundreds of
 * thousands of times per game, and the subset walk (up to 65536 IdentifyPattern calls)
 * was the single largest cost of the strong AI.
 *
 * A follow is never validated with the short-final exception, so the only patterns a
 * follow can form are the ones below; each condition is exactly "some subset of that
 * size is identified as that pattern and beats `previous`" (IdentifyPattern's order
 * was checked case by case: a 5-card three-of-r plus two non-r cards always reads as
 * triple-with-pair r, k consecutive exact pairs always read as consecutive pairs, and
 * L distinct consecutive ranks always read as a straight).  Planes are the one shape
 * whose identification depends on the kicker choice, so they keep the exhaustive
 * search; they are also rare as the pattern to beat.  tests/rules_tests checks the
 * two functions against each other on random hands.
 */
bool HasAnyFollowMove(const Cards *hand, const HandPattern *previous, int handSizeBeforePlay)
{
    int count[RANK_TWO + 1];
    const int n = hand->count;
    const int above = previous->mainRank;

    if (!HandPattern_IsValid(previous) || n == 0 || n >= 63) {
        return false;
    }
    if (previous->type == PATTERN_PLANE) {
        return HasAnyFollowMoveExhaustive(hand, previous, handSizeBeforePlay);
    }

    memset(count, 0, sizeof(count));
    for (int i = 0; i < n; ++i) {
        const Rank rank = hand->items[i].rank;

        if (rank < RANK_THREE || rank > RANK_TWO) {
            return HasAnyFollowMoveExhaustive(hand, previous, handSizeBeforePlay);
        }
        ++count[rank];
    }

    /* A bomb (four of 3..K; A and 2 cannot have four) beats every non-bomb and any
     * lower bomb. */
    for (int rank = RANK_THREE; rank <= RANK_KING; ++rank) {
        if (count[rank] >= 4 && (previous->type != PATTERN_BOMB || rank > above)) {
            return true;
        }
    }

    switch (previous->type) {
    case PATTERN_SINGLE:
        for (int rank = above + 1; rank <= RANK_TWO; ++rank) {
            if (count[rank] >= 1) {
                return true;
            }
        }
        return false;
    case PATTERN_PAIR:
        for (int rank = above + 1; rank <= RANK_TWO; ++rank) {
            if (count[rank] >= 2) {
                return true;
            }
        }
        return false;
    case PATTERN_TRIPLE_WITH_PAIR:
        for (int rank = above + 1; rank <= RANK_TWO; ++rank) {
            if (count[rank] >= 3 && n - count[rank] >= 2) {
                return true;
            }
        }
        return false;
    case PATTERN_STRAIGHT:
        return HasRun(count, previous->cardCount, 1, above);
    case PATTERN_CONSECUTIVE_PAIRS:
        return HasRun(count, previous->cardCount / 2, 2, above);
    default:
        /* Bombs were handled above; a short-final triple can only be beaten by one. */
        return false;
    }
}
