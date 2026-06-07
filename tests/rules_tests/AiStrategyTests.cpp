#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "game/AiStrategy.h"

using namespace pdk;
using tests::C;
using tests::CountRank;
using tests::FollowContext;
using tests::LeadContext;

TEST_CASE("ai triple with two keeps an existing pair as a pair") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(rules::Rank::Three),
        C(rules::Rank::Three, rules::Suit::Hearts),
        C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Four),
        C(rules::Rank::Four, rules::Suit::Hearts),
        C(rules::Rank::Five),
        C(rules::Rank::Six)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::TripleWithPair);
    CHECK(CountRank(choice.cards, rules::Rank::Three) == 3);
    CHECK(CountRank(choice.cards, rules::Rank::Four) == 0);
    CHECK(CountRank(choice.cards, rules::Rank::Five) == 1);
    CHECK(CountRank(choice.cards, rules::Rank::Six) == 1);
}

TEST_CASE("ai plane uses singleton kickers before breaking pairs or triples") {
    game::BasicAiStrategy ai;
    const auto previous = rules::IdentifyPattern({
        C(rules::Rank::Three),
        C(rules::Rank::Three, rules::Suit::Hearts),
        C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Four),
        C(rules::Rank::Four, rules::Suit::Hearts),
        C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Six),
        C(rules::Rank::Seven)
    }).pattern;
    const rules::Cards hand{
        C(rules::Rank::Four),
        C(rules::Rank::Four, rules::Suit::Hearts),
        C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Five),
        C(rules::Rank::Five, rules::Suit::Hearts),
        C(rules::Rank::Five, rules::Suit::Diamonds),
        C(rules::Rank::Six),
        C(rules::Rank::Six, rules::Suit::Hearts),
        C(rules::Rank::Seven),
        C(rules::Rank::Eight),
        C(rules::Rank::Nine),
        C(rules::Rank::Nine, rules::Suit::Hearts),
        C(rules::Rank::Nine, rules::Suit::Diamonds)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, FollowContext(previous, static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::Plane);
    CHECK(CountRank(choice.cards, rules::Rank::Four) == 3);
    CHECK(CountRank(choice.cards, rules::Rank::Five) == 3);
    CHECK(CountRank(choice.cards, rules::Rank::Six) == 0);
    CHECK(CountRank(choice.cards, rules::Rank::Seven) == 1);
    CHECK(CountRank(choice.cards, rules::Rank::Eight) == 1);
    CHECK(CountRank(choice.cards, rules::Rank::Nine) == 0);
}

TEST_CASE("ai follow chooses a higher singleton when it preserves a pair") {
    game::BasicAiStrategy ai;
    const auto previous = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    const rules::Cards hand{
        C(rules::Rank::Five),
        C(rules::Rank::Five, rules::Suit::Hearts),
        C(rules::Rank::Six)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, FollowContext(previous, static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::Single);
    CHECK(choice.pattern.mainRank == rules::Rank::Six);
    CHECK(CountRank(choice.cards, rules::Rank::Five) == 0);
    CHECK(CountRank(choice.cards, rules::Rank::Six) == 1);
}

TEST_CASE("ai lead avoids a small singleton when next player has one card") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(rules::Rank::Three),
        C(rules::Rank::Eight),
        C(rules::Rank::Ace)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size()), 1, 1));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::Single);
    CHECK(choice.pattern.mainRank == rules::Rank::Ace);
    CHECK(CountRank(choice.cards, rules::Rank::Three) == 0);
    CHECK(CountRank(choice.cards, rules::Rank::Ace) == 1);
}
