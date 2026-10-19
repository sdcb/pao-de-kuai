#pragma once

#include "rules/HandPattern.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct MoveValidation {
    bool ok;
    HandPattern pattern;
    char reason[PATTERN_REASON_CAP];
} MoveValidation;

/* `handSizeBeforePlay` is the hand size before the play; -1 means unknown. */
MoveValidation ValidateLead(const Cards *cards, int handSizeBeforePlay);
MoveValidation ValidateFollow(const Cards *cards, const HandPattern *previous,
                              int handSizeBeforePlay);
bool CanBeat(const HandPattern *candidate, const HandPattern *previous);
bool HasAnyFollowMove(const Cards *hand, const HandPattern *previous,
                      int handSizeBeforePlay);
/* The reference implementation HasAnyFollowMove must agree with: tries every subset
 * through ValidateFollow.  Exposed for the equivalence test and the plane fallback. */
bool HasAnyFollowMoveExhaustive(const Cards *hand, const HandPattern *previous,
                                int handSizeBeforePlay);

#ifdef __cplusplus
}
#endif
