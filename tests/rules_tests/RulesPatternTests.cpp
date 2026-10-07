#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "rules/CppCompat.h"

using namespace pdk;
using tests::C;

TEST_CASE("fixed deck has 48 cards with only spade two and no club ace") {
    const rules::Cards deck = rules::CreatePaoDeKuaiDeck();
    CHECK(deck.size() == 48);

    int twos = 0;
    int aces = 0;
    bool hasSpadeThree = false;
    bool hasClubAce = false;
    for (rules::Card card : deck) {
        if (card.rank == RANK_TWO) {
            twos++;
            CHECK(card.suit == SUIT_SPADES);
        }
        if (card.rank == RANK_ACE) {
            aces++;
            if (card.suit == SUIT_CLUBS) {
                hasClubAce = true;
            }
        }
        hasSpadeThree = hasSpadeThree || rules::IsSpadeThree(card);
    }
    CHECK(twos == 1);
    CHECK(aces == 3);
    CHECK_FALSE(hasClubAce);
    CHECK(hasSpadeThree);
}

TEST_CASE("spade three holder starts") {
    std::vector<rules::Cards> hands = {
        {C(RANK_FOUR)},
        {C(RANK_THREE, SUIT_SPADES)},
        {C(RANK_ACE)}
    };
    CHECK(rules::FindFirstPlayerBySpadeThree(hands) == 1);
}

TEST_CASE("recognizes core hand patterns") {
    CHECK(rules::IdentifyPattern({C(RANK_FIVE)}).pattern.type == PATTERN_SINGLE);
    CHECK(rules::IdentifyPattern({C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS)}).pattern.type == PATTERN_PAIR);

    auto straight = rules::IdentifyPattern({
        C(RANK_TEN), C(RANK_JACK), C(RANK_QUEEN), C(RANK_KING), C(RANK_ACE)
    });
    CHECK(straight.pattern.type == PATTERN_STRAIGHT);

    auto jqka2 = rules::IdentifyPattern({
        C(RANK_JACK), C(RANK_QUEEN), C(RANK_KING), C(RANK_ACE), C(RANK_TWO)
    });
    CHECK_FALSE(rules::IsValid(jqka2.pattern));

    auto twoPairs = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS)
    });
    CHECK(twoPairs.pattern.type == PATTERN_CONSECUTIVE_PAIRS);

    auto pairs = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS)
    });
    CHECK(pairs.pattern.type == PATTERN_CONSECUTIVE_PAIRS);

    auto tripleOne = rules::IdentifyPattern({
        C(RANK_EIGHT), C(RANK_EIGHT, SUIT_HEARTS), C(RANK_EIGHT, SUIT_DIAMONDS),
        C(RANK_FOUR)
    });
    CHECK_FALSE(rules::IsValid(tripleOne.pattern));

    auto triplePair = rules::IdentifyPattern({
        C(RANK_NINE), C(RANK_NINE, SUIT_HEARTS), C(RANK_NINE, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS)
    });
    CHECK(triplePair.pattern.type == PATTERN_TRIPLE_WITH_PAIR);

    auto tripleTwoLooseCards = rules::IdentifyPattern({
        C(RANK_JACK), C(RANK_JACK, SUIT_HEARTS), C(RANK_JACK, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_SEVEN)
    });
    CHECK(tripleTwoLooseCards.pattern.type == PATTERN_TRIPLE_WITH_PAIR);

    auto planeMinWings = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_SIX)
    });
    CHECK(planeMinWings.pattern.type == PATTERN_PLANE);
    CHECK(planeMinWings.pattern.groupCount == 2);
    CHECK(planeMinWings.pattern.mainRank == RANK_FOUR);

    auto planeMaxWings = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_SIX), C(RANK_SEVEN), C(RANK_EIGHT)
    });
    CHECK(planeMaxWings.pattern.type == PATTERN_PLANE);

    auto planeSplitBombs = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS), C(RANK_THREE, SUIT_CLUBS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS), C(RANK_FOUR, SUIT_CLUBS),
        C(RANK_FIVE), C(RANK_SIX)
    });
    CHECK(planeSplitBombs.pattern.type == PATTERN_PLANE);

    const rules::PatternResult fourWithExtra = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE)
    });
    CHECK_FALSE(rules::IsValid(fourWithExtra.pattern));

    const rules::PatternResult mixedRun = rules::IdentifyPattern({
        C(RANK_ACE), C(RANK_ACE, SUIT_HEARTS), C(RANK_ACE, SUIT_DIAMONDS),
        C(RANK_TWO), C(RANK_TWO, SUIT_HEARTS), C(RANK_TWO, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_SIX)
    });
    CHECK_FALSE(rules::IsValid(mixedRun.pattern));

    const rules::PatternResult bareTriple = rules::IdentifyPattern({
        C(RANK_NINE), C(RANK_NINE, SUIT_HEARTS), C(RANK_NINE, SUIT_DIAMONDS)
    });
    CHECK_FALSE(rules::IsValid(bareTriple.pattern));
    CHECK(rules::IdentifyPattern({
        C(RANK_NINE), C(RANK_NINE, SUIT_HEARTS), C(RANK_NINE, SUIT_DIAMONDS)
    }, 3, true).pattern.lastHandShort);
}

TEST_CASE("bombs cannot be played as four with three") {
    auto bomb = rules::IdentifyPattern({
        C(RANK_KING), C(RANK_KING, SUIT_HEARTS),
        C(RANK_KING, SUIT_DIAMONDS), C(RANK_KING, SUIT_CLUBS)
    });
    CHECK(bomb.pattern.type == PATTERN_BOMB);

    auto aceBomb = rules::IdentifyPattern({
        C(RANK_ACE), C(RANK_ACE, SUIT_HEARTS),
        C(RANK_ACE, SUIT_DIAMONDS), C(RANK_ACE, SUIT_CLUBS)
    });
    CHECK_FALSE(rules::IsValid(aceBomb.pattern));

    auto fourWithThree = rules::IdentifyPattern({
        C(RANK_SIX), C(RANK_SIX, SUIT_HEARTS),
        C(RANK_SIX, SUIT_DIAMONDS), C(RANK_SIX, SUIT_CLUBS),
        C(RANK_THREE), C(RANK_FOUR), C(RANK_FIVE)
    });
    CHECK_FALSE(rules::IsValid(fourWithThree.pattern));
}

TEST_CASE("move comparison follows fixed rules") {
    const auto pair5 = rules::IdentifyPattern({C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS)}).pattern;
    const auto pair6 = rules::IdentifyPattern({C(RANK_SIX), C(RANK_SIX, SUIT_HEARTS)}).pattern;
    CHECK(rules::CanBeat(pair6, pair5));
    CHECK_FALSE(rules::CanBeat(pair5, pair6));

    const auto straightLow = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_FOUR), C(RANK_FIVE), C(RANK_SIX), C(RANK_SEVEN)
    }).pattern;
    const auto straightHigh = rules::IdentifyPattern({
        C(RANK_FOUR), C(RANK_FIVE), C(RANK_SIX), C(RANK_SEVEN), C(RANK_EIGHT)
    }).pattern;
    CHECK(rules::CanBeat(straightHigh, straightLow));

    const auto singleAce = rules::IdentifyPattern({C(RANK_ACE)}).pattern;
    const auto bomb3 = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS),
        C(RANK_THREE, SUIT_DIAMONDS), C(RANK_THREE, SUIT_CLUBS)
    }).pattern;
    CHECK(rules::CanBeat(bomb3, singleAce));

    const auto planeLow = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_QUEEN), C(RANK_KING)
    }).pattern;
    const auto planeHigh = rules::IdentifyPattern({
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS), C(RANK_FIVE, SUIT_DIAMONDS),
        C(RANK_SIX), C(RANK_SEVEN)
    }).pattern;
    const auto planeThreeGroups = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS), C(RANK_FIVE, SUIT_DIAMONDS),
        C(RANK_SIX), C(RANK_SEVEN), C(RANK_EIGHT)
    }).pattern;
    CHECK(rules::CanBeat(planeHigh, planeLow));
    CHECK_FALSE(rules::CanBeat(planeLow, planeHigh));
    CHECK_FALSE(rules::CanBeat(planeHigh, planeThreeGroups));
    CHECK_FALSE(rules::CanBeat(planeThreeGroups, planeHigh));

    const auto tripleTwo = rules::IdentifyPattern({
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_FOUR, SUIT_DIAMONDS), C(RANK_FIVE), C(RANK_SIX)
    }).pattern;
    CHECK(rules::IsValid(tripleTwo));
}

TEST_CASE("lead validation allows short final triples and plane but follow validation does not") {
    const rules::Cards bareTriple{
        C(RANK_NINE), C(RANK_NINE, SUIT_HEARTS), C(RANK_NINE, SUIT_DIAMONDS)
    };
    const auto bareTripleLead = rules::ValidateLead(bareTriple, static_cast<int>(bareTriple.size()));
    CHECK(bareTripleLead.ok);
    CHECK(bareTripleLead.pattern.type == PATTERN_TRIPLE_WITH_ONE);
    CHECK(bareTripleLead.pattern.lastHandShort);

    const rules::Cards tripleWithOne{
        C(RANK_NINE), C(RANK_NINE, SUIT_HEARTS), C(RANK_NINE, SUIT_DIAMONDS),
        C(RANK_FOUR)
    };
    const auto tripleWithOneNormal = rules::ValidateLead(tripleWithOne, 5);
    CHECK_FALSE(tripleWithOneNormal.ok);

    const auto tripleWithOneFinal = rules::ValidateLead(tripleWithOne, static_cast<int>(tripleWithOne.size()));
    CHECK(tripleWithOneFinal.ok);
    CHECK(tripleWithOneFinal.pattern.type == PATTERN_TRIPLE_WITH_ONE);
    CHECK(tripleWithOneFinal.pattern.lastHandShort);

    const auto previousTripleWithPair = rules::IdentifyPattern({
        C(RANK_EIGHT), C(RANK_EIGHT, SUIT_HEARTS), C(RANK_EIGHT, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_SIX)
    }).pattern;
    const auto tripleWithOneFollow = rules::ValidateFollow(tripleWithOne, previousTripleWithPair, static_cast<int>(tripleWithOne.size()));
    CHECK_FALSE(tripleWithOneFollow.ok);

    const rules::Cards shortPlane{
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS)
    };
    const auto lead = rules::ValidateLead(shortPlane, static_cast<int>(shortPlane.size()));
    CHECK(lead.ok);
    CHECK(lead.pattern.type == PATTERN_PLANE);
    CHECK(lead.pattern.groupCount == 2);
    CHECK(lead.pattern.lastHandShort);

    const auto previous = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_SIX)
    }).pattern;
    const auto follow = rules::ValidateFollow(shortPlane, previous, static_cast<int>(shortPlane.size()));
    CHECK_FALSE(follow.ok);
}

TEST_CASE("follow existence check uses rules without ai scoring") {
    const auto singleFour = rules::IdentifyPattern({C(RANK_FOUR)}).pattern;
    CHECK(rules::HasAnyFollowMove({C(RANK_FIVE)}, singleFour, 1));
    CHECK_FALSE(rules::HasAnyFollowMove({C(RANK_THREE)}, singleFour, 1));

    const auto singleAce = rules::IdentifyPattern({C(RANK_ACE)}).pattern;
    CHECK(rules::HasAnyFollowMove({
        C(RANK_THREE),
        C(RANK_THREE, SUIT_HEARTS),
        C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_THREE, SUIT_CLUBS)
    }, singleAce, 4));

    const auto straightSix = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_FOUR), C(RANK_FIVE),
        C(RANK_SIX), C(RANK_SEVEN), C(RANK_EIGHT)
    }).pattern;
    CHECK_FALSE(rules::HasAnyFollowMove({
        C(RANK_TEN), C(RANK_JACK), C(RANK_QUEEN), C(RANK_KING), C(RANK_ACE)
    }, straightSix, 5));

    const auto planeThreeGroups = rules::IdentifyPattern({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS), C(RANK_FIVE, SUIT_DIAMONDS),
        C(RANK_SIX), C(RANK_SEVEN), C(RANK_EIGHT)
    }).pattern;
    CHECK_FALSE(rules::HasAnyFollowMove({
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS), C(RANK_FIVE, SUIT_DIAMONDS),
        C(RANK_SEVEN), C(RANK_EIGHT)
    }, planeThreeGroups, 8));
}
