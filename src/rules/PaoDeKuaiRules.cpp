#include "rules/PaoDeKuaiRules.h"

namespace pdk::rules {

RuleSet FixedPaoDeKuaiRuleSet() {
    return RuleSet{};
}

Cards PaoDeKuaiRules::CreateDeck() const {
    return CreatePaoDeKuaiDeck();
}

MoveValidation PaoDeKuaiRules::ValidateLeadMove(const Cards& cards, int handSizeBeforePlay) const {
    return ValidateLead(cards, handSizeBeforePlay);
}

MoveValidation PaoDeKuaiRules::ValidateFollowMove(
    const Cards& cards,
    const HandPattern& previous,
    int handSizeBeforePlay) const {
    return ValidateFollow(cards, previous, handSizeBeforePlay);
}

} // namespace pdk::rules
