#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* The fixed rule set of this game.  Nothing here is configurable at runtime;
 * AGENTS.md treats the rules as frozen, so this exists to keep the constants in
 * one reviewable place. */
typedef struct RuleSet {
    int playerCount;
    int deckSize;
    bool spadeThreeStarts;
    bool firstMoveMustContainSpadeThree;
    bool bombsBeatAnyNonBomb;
    int bombWinnerScore;
    int bombLoserScore;
    int springLoserPenalty;
} RuleSet;

RuleSet FixedPaoDeKuaiRuleSet(void);

#ifdef __cplusplus
}
#endif
