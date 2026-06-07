#pragma once

namespace pdk::rules {

struct RuleSet {
    int playerCount{3};
    int deckSize{48};
    bool spadeThreeStarts{true};
    bool firstMoveMustContainSpadeThree{false};
    bool bombsBeatAnyNonBomb{true};
    int bombWinnerScore{20};
    int bombLoserScore{-10};
    int springLoserPenalty{32};
};

RuleSet FixedPaoDeKuaiRuleSet();

} // namespace pdk::rules
