#pragma once

#include "game/AiStrategy.h"
#include "rules/Card.h"

#include <algorithm>

namespace pdk::tests {

inline rules::Card C(rules::Rank rank, rules::Suit suit = rules::Suit::Spades) {
    return {rank, suit};
}

inline int CountRank(const rules::Cards& cards, rules::Rank rank) {
    return static_cast<int>(std::count_if(cards.begin(), cards.end(), [rank](rules::Card card) {
        return card.rank == rank;
    }));
}

inline game::AiContext LeadContext(int ownRemaining, int nextRemaining = 16, int minOpponentRemaining = 16) {
    game::AiContext context;
    context.leading = true;
    context.ownRemainingCards = ownRemaining;
    context.nextPlayerRemainingCards = nextRemaining;
    context.minOpponentRemainingCards = minOpponentRemaining;
    context.remainingCards = {ownRemaining, nextRemaining, minOpponentRemaining};
    return context;
}

inline game::AiContext FollowContext(const rules::HandPattern& previous, int ownRemaining = 16) {
    game::AiContext context;
    context.leading = false;
    context.previous = previous;
    context.ownRemainingCards = ownRemaining;
    context.nextPlayerRemainingCards = 16;
    context.minOpponentRemainingCards = 16;
    context.remainingCards = {ownRemaining, 16, 16};
    return context;
}

} // namespace pdk::tests
