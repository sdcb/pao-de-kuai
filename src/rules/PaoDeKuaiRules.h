#pragma once

#include "rules/Deck.h"
#include "rules/MoveValidator.h"
#include "rules/RuleSet.h"
#include "rules/Scoring.h"

namespace pdk::rules {

class PaoDeKuaiRules {
public:
    const RuleSet& Rule() const { return rule_; }

    Cards CreateDeck() const;
    MoveValidation ValidateLeadMove(const Cards& cards, int handSizeBeforePlay = -1) const;
    MoveValidation ValidateFollowMove(
        const Cards& cards,
        const HandPattern& previous,
        int handSizeBeforePlay = -1) const;

private:
    RuleSet rule_{FixedPaoDeKuaiRuleSet()};
};

} // namespace pdk::rules
