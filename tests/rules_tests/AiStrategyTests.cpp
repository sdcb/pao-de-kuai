#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "TestWeakAiStrategy.h"
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

TEST_CASE("ai lead prefers triple with two loose kickers over triple with one") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(rules::Rank::Six),
        C(rules::Rank::Six, rules::Suit::Hearts),
        C(rules::Rank::Six, rules::Suit::Diamonds),
        C(rules::Rank::Seven),
        C(rules::Rank::Nine),
        C(rules::Rank::King)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::TripleWithPair);
    CHECK(choice.cards.size() == 5);
    CHECK(CountRank(choice.cards, rules::Rank::Six) == 3);
}

TEST_CASE("ai lead uses the full consecutive pairs run") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(rules::Rank::Three),
        C(rules::Rank::Three, rules::Suit::Hearts),
        C(rules::Rank::Four),
        C(rules::Rank::Four, rules::Suit::Hearts),
        C(rules::Rank::Five),
        C(rules::Rank::Five, rules::Suit::Hearts)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::ConsecutivePairs);
    CHECK(choice.pattern.cardCount == 6);
    CHECK(choice.cards.size() == 6);
}

TEST_CASE("ai lead keeps extending consecutive pairs before leaving loose singles") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(rules::Rank::Three),
        C(rules::Rank::Three, rules::Suit::Hearts),
        C(rules::Rank::Four),
        C(rules::Rank::Four, rules::Suit::Hearts),
        C(rules::Rank::Five),
        C(rules::Rank::Five, rules::Suit::Hearts),
        C(rules::Rank::Seven),
        C(rules::Rank::Nine)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::ConsecutivePairs);
    CHECK(choice.pattern.cardCount == 6);
    CHECK(choice.cards.size() == 6);
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

TEST_CASE("ai lead uses king instead of eight when next player has one card") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(rules::Rank::Eight),
        C(rules::Rank::King)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size()), 1, 1));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::Single);
    CHECK(choice.pattern.mainRank == rules::Rank::King);
}

TEST_CASE("ai follow uses high singleton when next player has one card") {
    game::BasicAiStrategy ai;
    const auto previous = rules::IdentifyPattern({C(rules::Rank::Seven)}).pattern;
    rules::Cards hand{
        C(rules::Rank::Eight),
        C(rules::Rank::King)
    };
    game::AiContext context = FollowContext(previous, static_cast<int>(hand.size()));
    context.nextPlayerRemainingCards = 1;
    context.minOpponentRemainingCards = 1;

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::Single);
    CHECK(choice.pattern.mainRank == rules::Rank::King);
}

TEST_CASE("ai normal lead does not throw high control singleton first") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(rules::Rank::Three),
        C(rules::Rank::Three, rules::Suit::Hearts),
        C(rules::Rank::Six),
        C(rules::Rank::Seven),
        C(rules::Rank::Seven, rules::Suit::Hearts),
        C(rules::Rank::Seven, rules::Suit::Diamonds),
        C(rules::Rank::Eight),
        C(rules::Rank::Nine),
        C(rules::Rank::Ten),
        C(rules::Rank::Ace)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size()), 10, 10));

    CHECK_FALSE(choice.pass);
    const bool throwsAceSingleton = choice.pattern.type == rules::PatternType::Single && choice.pattern.mainRank == rules::Rank::Ace;
    CHECK_FALSE(throwsAceSingleton);
    CHECK(CountRank(choice.cards, rules::Rank::Ace) == 0);
    CHECK(choice.pattern.mainRank < rules::Rank::Ace);
}

TEST_CASE("ai lead uses proven safe singleton and keeps higher control cards") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(rules::Rank::Queen),
        C(rules::Rank::King),
        C(rules::Rank::Ace),
        C(rules::Rank::Two)
    };
    game::AiContext context = LeadContext(static_cast<int>(hand.size()), 10, 10);
    context.currentPlayerIndex = 1;
    context.remainingCards = {10, static_cast<int>(hand.size()), 10};
    context.passObservations[0] = game::PassObservation{
        rules::HandPattern{rules::PatternType::Single, rules::Rank::Queen, 1},
        10
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::Single);
    CHECK(choice.pattern.mainRank == rules::Rank::Queen);
    CHECK(CountRank(choice.cards, rules::Rank::Ace) == 0);
    CHECK(CountRank(choice.cards, rules::Rank::Two) == 0);
}

TEST_CASE("ai urgent lead can still use high singleton despite safe singleton observation") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(rules::Rank::Queen),
        C(rules::Rank::King),
        C(rules::Rank::Ace)
    };
    game::AiContext context = LeadContext(static_cast<int>(hand.size()), 1, 1);
    context.currentPlayerIndex = 1;
    context.remainingCards = {1, static_cast<int>(hand.size()), 8};
    context.passObservations[2] = game::PassObservation{
        rules::HandPattern{rules::PatternType::Single, rules::Rank::Queen, 1},
        8
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::Single);
    CHECK(choice.pattern.mainRank == rules::Rank::Ace);
}

TEST_CASE("test weak ai lead ignores one-card defense and plays the lowest singleton") {
    tests::TestWeakAiStrategy ai;
    const rules::Cards hand{
        C(rules::Rank::Three),
        C(rules::Rank::Eight),
        C(rules::Rank::Ace)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size()), 1, 1));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::Single);
    CHECK(choice.pattern.mainRank == rules::Rank::Three);
}

TEST_CASE("test weak ai follow breaks a pair to use the lowest beating singleton") {
    tests::TestWeakAiStrategy ai;
    const auto previous = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    const rules::Cards hand{
        C(rules::Rank::Five),
        C(rules::Rank::Five, rules::Suit::Hearts),
        C(rules::Rank::Six)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, FollowContext(previous, static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == rules::PatternType::Single);
    CHECK(choice.pattern.mainRank == rules::Rank::Five);
    CHECK(CountRank(choice.cards, rules::Rank::Five) == 1);
    CHECK(CountRank(choice.cards, rules::Rank::Six) == 0);
}
