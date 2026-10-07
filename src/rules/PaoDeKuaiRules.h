#pragma once

#include "rules/Deck.h"
#include "rules/MoveValidator.h"
#include "rules/RuleSet.h"
#include "rules/Scoring.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The rule book for one table.  The old type was a class with a `RuleSet` member;
 * there is no state beyond that, so it stays a value type created by
 * PaoDeKuaiRules_Create() and every method takes it by pointer.
 */
typedef struct PaoDeKuaiRules {
    RuleSet rule;
} PaoDeKuaiRules;

PaoDeKuaiRules PaoDeKuaiRules_Create(void);
Cards PaoDeKuaiRules_CreateDeck(const PaoDeKuaiRules *rules);
/* `handSizeBeforePlay` is the hand size before the play; -1 means unknown
 * (the old defaulted argument). */
MoveValidation PaoDeKuaiRules_ValidateLeadMove(const PaoDeKuaiRules *rules,
                                               const Cards *cards,
                                               int handSizeBeforePlay);
MoveValidation PaoDeKuaiRules_ValidateFollowMove(const PaoDeKuaiRules *rules,
                                                 const Cards *cards,
                                                 const HandPattern *previous,
                                                 int handSizeBeforePlay);

#ifdef __cplusplus
}
#endif
