#include "rules/PaoDeKuaiRules.h"

RuleSet FixedPaoDeKuaiRuleSet(void)
{
    RuleSet rules;

    rules.playerCount = 3;
    rules.deckSize = 48;
    rules.spadeThreeStarts = true;
    rules.firstMoveMustContainSpadeThree = false;
    rules.bombsBeatAnyNonBomb = true;
    rules.bombWinnerScore = 20;
    rules.bombLoserScore = -10;
    rules.springLoserPenalty = 32;
    return rules;
}

PaoDeKuaiRules PaoDeKuaiRules_Create(void)
{
    PaoDeKuaiRules rules;
    rules.rule = FixedPaoDeKuaiRuleSet();
    return rules;
}

Cards PaoDeKuaiRules_CreateDeck(const PaoDeKuaiRules *rules)
{
    (void)rules;
    return CreatePaoDeKuaiDeck();
}

MoveValidation PaoDeKuaiRules_ValidateLeadMove(const PaoDeKuaiRules *rules,
                                               const Cards *cards,
                                               int handSizeBeforePlay)
{
    (void)rules;
    return ValidateLead(cards, handSizeBeforePlay);
}

MoveValidation PaoDeKuaiRules_ValidateFollowMove(const PaoDeKuaiRules *rules,
                                                 const Cards *cards,
                                                 const HandPattern *previous,
                                                 int handSizeBeforePlay)
{
    (void)rules;
    return ValidateFollow(cards, previous, handSizeBeforePlay);
}
