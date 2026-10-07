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
        C(RANK_THREE),
        C(RANK_THREE, SUIT_HEARTS),
        C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR),
        C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_FIVE),
        C(RANK_SIX)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_TRIPLE_WITH_PAIR);
    CHECK(CountRank(choice.cards, RANK_THREE) == 3);
    CHECK(CountRank(choice.cards, RANK_FOUR) == 0);
    CHECK(CountRank(choice.cards, RANK_FIVE) == 1);
    CHECK(CountRank(choice.cards, RANK_SIX) == 1);
}

TEST_CASE("ai lead prefers triple with two loose kickers over triple with one") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(RANK_SIX),
        C(RANK_SIX, SUIT_HEARTS),
        C(RANK_SIX, SUIT_DIAMONDS),
        C(RANK_SEVEN),
        C(RANK_NINE),
        C(RANK_KING),
        C(RANK_ACE)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_TRIPLE_WITH_PAIR);
    CHECK(choice.cards.size() == 5);
    CHECK(CountRank(choice.cards, RANK_SIX) == 3);
    CHECK(CountRank(choice.cards, RANK_SEVEN) == 1);
    CHECK(CountRank(choice.cards, RANK_NINE) == 1);
    CHECK(CountRank(choice.cards, RANK_KING) == 0);
    CHECK(CountRank(choice.cards, RANK_ACE) == 0);
}

TEST_CASE("strong ai triple with two preserves high control kickers") {
    game::StrongAiStrategy ai;
    const rules::Cards hand{
        C(RANK_SIX),
        C(RANK_SIX, SUIT_HEARTS),
        C(RANK_SIX, SUIT_DIAMONDS),
        C(RANK_SEVEN),
        C(RANK_NINE),
        C(RANK_KING),
        C(RANK_ACE),
        C(RANK_TWO)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_TRIPLE_WITH_PAIR);
    CHECK(CountRank(choice.cards, RANK_SIX) == 3);
    CHECK(CountRank(choice.cards, RANK_SEVEN) == 1);
    CHECK(CountRank(choice.cards, RANK_NINE) == 1);
    CHECK(CountRank(choice.cards, RANK_KING) == 0);
    CHECK(CountRank(choice.cards, RANK_ACE) == 0);
    CHECK(CountRank(choice.cards, RANK_TWO) == 0);
}

TEST_CASE("ai lead uses the full consecutive pairs run") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(RANK_THREE),
        C(RANK_THREE, SUIT_HEARTS),
        C(RANK_FOUR),
        C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_FIVE),
        C(RANK_FIVE, SUIT_HEARTS)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_CONSECUTIVE_PAIRS);
    CHECK(choice.pattern.cardCount == 6);
    CHECK(choice.cards.size() == 6);
}

TEST_CASE("ai lead keeps extending consecutive pairs before leaving loose singles") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(RANK_THREE),
        C(RANK_THREE, SUIT_HEARTS),
        C(RANK_FOUR),
        C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_FIVE),
        C(RANK_FIVE, SUIT_HEARTS),
        C(RANK_SEVEN),
        C(RANK_NINE)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_CONSECUTIVE_PAIRS);
    CHECK(choice.pattern.cardCount == 6);
    CHECK(choice.cards.size() == 6);
}

TEST_CASE("ai plane uses singleton kickers before breaking pairs or triples") {
    game::BasicAiStrategy ai;
    const auto previous = rules::IdentifyPattern({
        C(RANK_THREE),
        C(RANK_THREE, SUIT_HEARTS),
        C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR),
        C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_SIX),
        C(RANK_SEVEN)
    }).pattern;
    const rules::Cards hand{
        C(RANK_FOUR),
        C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE),
        C(RANK_FIVE, SUIT_HEARTS),
        C(RANK_FIVE, SUIT_DIAMONDS),
        C(RANK_SIX),
        C(RANK_SIX, SUIT_HEARTS),
        C(RANK_SEVEN),
        C(RANK_EIGHT),
        C(RANK_NINE),
        C(RANK_NINE, SUIT_HEARTS),
        C(RANK_NINE, SUIT_DIAMONDS)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, FollowContext(previous, static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_PLANE);
    CHECK(CountRank(choice.cards, RANK_FOUR) == 3);
    CHECK(CountRank(choice.cards, RANK_FIVE) == 3);
    CHECK(CountRank(choice.cards, RANK_SIX) == 0);
    CHECK(CountRank(choice.cards, RANK_SEVEN) == 1);
    CHECK(CountRank(choice.cards, RANK_EIGHT) == 1);
    CHECK(CountRank(choice.cards, RANK_NINE) == 0);
}

TEST_CASE("ai follow chooses a higher singleton when it preserves a pair") {
    game::BasicAiStrategy ai;
    const auto previous = rules::IdentifyPattern({C(RANK_FOUR)}).pattern;
    const rules::Cards hand{
        C(RANK_FIVE),
        C(RANK_FIVE, SUIT_HEARTS),
        C(RANK_SIX)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, FollowContext(previous, static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_SIX);
    CHECK(CountRank(choice.cards, RANK_FIVE) == 0);
    CHECK(CountRank(choice.cards, RANK_SIX) == 1);
}

TEST_CASE("ai lead avoids a small singleton when next player has one card") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(RANK_THREE),
        C(RANK_EIGHT),
        C(RANK_ACE)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size()), 1, 1));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_ACE);
    CHECK(CountRank(choice.cards, RANK_THREE) == 0);
    CHECK(CountRank(choice.cards, RANK_ACE) == 1);
}

TEST_CASE("ai lead uses king instead of eight when next player has one card") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(RANK_EIGHT),
        C(RANK_KING)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size()), 1, 1));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_KING);
}

TEST_CASE("ai follow uses high singleton when next player has one card") {
    game::BasicAiStrategy ai;
    const auto previous = rules::IdentifyPattern({C(RANK_SEVEN)}).pattern;
    rules::Cards hand{
        C(RANK_EIGHT),
        C(RANK_KING)
    };
    game::AiContext context = FollowContext(previous, static_cast<int>(hand.size()));
    context.nextPlayerRemainingCards = 1;
    context.minOpponentRemainingCards = 1;

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_KING);
}

TEST_CASE("ai normal lead does not throw high control singleton first") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(RANK_THREE),
        C(RANK_THREE, SUIT_HEARTS),
        C(RANK_SIX),
        C(RANK_SEVEN),
        C(RANK_SEVEN, SUIT_HEARTS),
        C(RANK_SEVEN, SUIT_DIAMONDS),
        C(RANK_EIGHT),
        C(RANK_NINE),
        C(RANK_TEN),
        C(RANK_ACE)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size()), 10, 10));

    CHECK_FALSE(choice.pass);
    const bool throwsAceSingleton = choice.pattern.type == PATTERN_SINGLE && choice.pattern.mainRank == RANK_ACE;
    CHECK_FALSE(throwsAceSingleton);
    CHECK(CountRank(choice.cards, RANK_ACE) == 0);
    CHECK(choice.pattern.mainRank < RANK_ACE);
}

TEST_CASE("ai lead uses proven safe singleton and keeps higher control cards") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(RANK_QUEEN),
        C(RANK_KING),
        C(RANK_ACE),
        C(RANK_TWO)
    };
    game::AiContext context = LeadContext(static_cast<int>(hand.size()), 10, 10);
    context.currentPlayerIndex = 1;
    context.remainingCards = {10, static_cast<int>(hand.size()), 10};
    context.passObservations[0] = game::PassObservation{
        rules::HandPattern{PATTERN_SINGLE, RANK_QUEEN, 1},
        10
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_QUEEN);
    CHECK(CountRank(choice.cards, RANK_ACE) == 0);
    CHECK(CountRank(choice.cards, RANK_TWO) == 0);
}

TEST_CASE("ai urgent lead can still use high singleton despite safe singleton observation") {
    game::BasicAiStrategy ai;
    const rules::Cards hand{
        C(RANK_QUEEN),
        C(RANK_KING),
        C(RANK_ACE)
    };
    game::AiContext context = LeadContext(static_cast<int>(hand.size()), 1, 1);
    context.currentPlayerIndex = 1;
    context.remainingCards = {1, static_cast<int>(hand.size()), 8};
    context.passObservations[2] = game::PassObservation{
        rules::HandPattern{PATTERN_SINGLE, RANK_QUEEN, 1},
        8
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_ACE);
}

TEST_CASE("test weak ai lead ignores one-card defense and plays the lowest singleton") {
    tests::TestWeakAiStrategy ai;
    const rules::Cards hand{
        C(RANK_THREE),
        C(RANK_EIGHT),
        C(RANK_ACE)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size()), 1, 1));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_THREE);
}

TEST_CASE("test weak ai follow breaks a pair to use the lowest beating singleton") {
    tests::TestWeakAiStrategy ai;
    const auto previous = rules::IdentifyPattern({C(RANK_FOUR)}).pattern;
    const rules::Cards hand{
        C(RANK_FIVE),
        C(RANK_FIVE, SUIT_HEARTS),
        C(RANK_SIX)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, FollowContext(previous, static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_FIVE);
    CHECK(CountRank(choice.cards, RANK_FIVE) == 1);
    CHECK(CountRank(choice.cards, RANK_SIX) == 0);
}

TEST_CASE("strong ai uses proven pass information for a safe singleton lead") {
    game::StrongAiStrategy ai;
    const rules::Cards hand{
        C(RANK_QUEEN),
        C(RANK_KING),
        C(RANK_ACE),
        C(RANK_TWO)
    };
    game::AiContext context = LeadContext(static_cast<int>(hand.size()), 8, 8);
    context.currentPlayerIndex = 1;
    context.remainingCards = {8, static_cast<int>(hand.size()), 8};
    context.passObservations[0] = game::PassObservation{
        rules::HandPattern{PATTERN_SINGLE, RANK_QUEEN, 1},
        8
    };
    context.passObservations[2] = game::PassObservation{
        rules::HandPattern{PATTERN_SINGLE, RANK_QUEEN, 1},
        8
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_QUEEN);
}

TEST_CASE("strong ai urgent follow uses a high singleton blocker") {
    game::StrongAiStrategy ai;
    const auto previous = rules::IdentifyPattern({C(RANK_SEVEN)}).pattern;
    rules::Cards hand{
        C(RANK_EIGHT),
        C(RANK_KING)
    };
    game::AiContext context = FollowContext(previous, static_cast<int>(hand.size()));
    context.nextPlayerRemainingCards = 1;
    context.minOpponentRemainingCards = 1;

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_KING);
}

TEST_CASE("strong ai lead blocks a one-card opponent even when not next player") {
    game::StrongAiStrategy ai;
    const rules::Cards hand{
        C(RANK_THREE),
        C(RANK_EIGHT),
        C(RANK_ACE)
    };
    game::AiContext context = LeadContext(static_cast<int>(hand.size()), 6, 1);
    context.remainingCards = {static_cast<int>(hand.size()), 6, 1};

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_ACE);
}

TEST_CASE("strong ai follow blocks a one-card opponent even when not next player") {
    game::StrongAiStrategy ai;
    const auto previous = rules::IdentifyPattern({C(RANK_SEVEN)}).pattern;
    rules::Cards hand{
        C(RANK_EIGHT),
        C(RANK_KING)
    };
    game::AiContext context = FollowContext(previous, static_cast<int>(hand.size()));
    context.nextPlayerRemainingCards = 6;
    context.minOpponentRemainingCards = 1;
    context.remainingCards = {static_cast<int>(hand.size()), 6, 1};

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_KING);
}

TEST_CASE("strong ai early lead prefers a multi-card plan") {
    game::StrongAiStrategy ai;
    const rules::Cards hand{
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_SEVEN), C(RANK_SEVEN, SUIT_HEARTS),
        C(RANK_FIVE), C(RANK_NINE), C(RANK_JACK),
        C(RANK_QUEEN), C(RANK_KING), C(RANK_ACE), C(RANK_TWO)
    };
    game::AiContext context = LeadContext(static_cast<int>(hand.size()), 13, 13);
    context.currentPlayerIndex = 1;
    context.roundLeaderIndex = 1;
    context.remainingCards = {13, static_cast<int>(hand.size()), 13};

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.cards.size() >= 4);
    CHECK(choice.pattern.type != PATTERN_SINGLE);
}

TEST_CASE("strong ai does not use three cards from a bomb as a triple") {
    game::StrongAiStrategy ai;
    const rules::Cards hand{
        C(RANK_SEVEN), C(RANK_SEVEN, SUIT_HEARTS),
        C(RANK_SEVEN, SUIT_DIAMONDS), C(RANK_SEVEN, SUIT_CLUBS),
        C(RANK_THREE), C(RANK_FOUR), C(RANK_ACE)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    const int bombRankUsed = CountRank(choice.cards, RANK_SEVEN);
    CHECK((bombRankUsed == 0 || bombRankUsed == 4));
}

TEST_CASE("strong ai ordinary singleton follow uses the lower beater") {
    game::StrongAiStrategy ai;
    const auto previous = rules::IdentifyPattern({C(RANK_FOUR)}).pattern;
    const rules::Cards hand{
        C(RANK_FIVE), C(RANK_SIX), C(RANK_KING), C(RANK_ACE)
    };
    game::AiContext context = FollowContext(previous, static_cast<int>(hand.size()));
    context.minOpponentRemainingCards = 6;
    context.nextPlayerRemainingCards = 6;

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.type == PATTERN_SINGLE);
    CHECK(choice.pattern.mainRank == RANK_FIVE);
}

TEST_CASE("strong ai urgent singleton follow keeps the high blocker") {
    game::StrongAiStrategy ai;
    const auto previous = rules::IdentifyPattern({C(RANK_SEVEN)}).pattern;
    const rules::Cards hand{C(RANK_EIGHT), C(RANK_KING)};
    game::AiContext context = FollowContext(previous, static_cast<int>(hand.size()));
    context.minOpponentRemainingCards = 1;
    context.nextPlayerRemainingCards = 1;

    const game::AiMoveChoice choice = ai.ChooseMove(hand, context);

    CHECK_FALSE(choice.pass);
    CHECK(choice.pattern.mainRank == RANK_KING);
}

TEST_CASE("strong ai midgame lead does not split a pair when a singleton is available") {
    game::StrongAiStrategy ai;
    const rules::Cards hand{
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_EIGHT), C(RANK_NINE),
        C(RANK_KING), C(RANK_ACE)
    };

    const game::AiMoveChoice choice = ai.ChooseMove(hand, LeadContext(static_cast<int>(hand.size())));

    CHECK_FALSE(choice.pass);
    if (choice.pattern.type == PATTERN_SINGLE) {
        CHECK(choice.pattern.mainRank != RANK_FOUR);
    }
}
