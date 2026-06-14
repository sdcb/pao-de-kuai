#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "rules/Deck.h"
#include "rules/MoveValidator.h"

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
        if (card.rank == rules::Rank::Two) {
            twos++;
            CHECK(card.suit == rules::Suit::Spades);
        }
        if (card.rank == rules::Rank::Ace) {
            aces++;
            if (card.suit == rules::Suit::Clubs) {
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
        {C(rules::Rank::Four)},
        {C(rules::Rank::Three, rules::Suit::Spades)},
        {C(rules::Rank::Ace)}
    };
    CHECK(rules::FindFirstPlayerBySpadeThree(hands) == 1);
}

TEST_CASE("recognizes core hand patterns") {
    CHECK(rules::IdentifyPattern({C(rules::Rank::Five)}).pattern.type == rules::PatternType::Single);
    CHECK(rules::IdentifyPattern({C(rules::Rank::Five), C(rules::Rank::Five, rules::Suit::Hearts)}).pattern.type == rules::PatternType::Pair);

    auto straight = rules::IdentifyPattern({
        C(rules::Rank::Ten), C(rules::Rank::Jack), C(rules::Rank::Queen), C(rules::Rank::King), C(rules::Rank::Ace)
    });
    CHECK(straight.pattern.type == rules::PatternType::Straight);

    auto jqka2 = rules::IdentifyPattern({
        C(rules::Rank::Jack), C(rules::Rank::Queen), C(rules::Rank::King), C(rules::Rank::Ace), C(rules::Rank::Two)
    });
    CHECK_FALSE(jqka2.pattern.IsValid());

    auto twoPairs = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts)
    });
    CHECK(twoPairs.pattern.type == rules::PatternType::ConsecutivePairs);

    auto pairs = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts),
        C(rules::Rank::Five), C(rules::Rank::Five, rules::Suit::Hearts)
    });
    CHECK(pairs.pattern.type == rules::PatternType::ConsecutivePairs);

    auto tripleOne = rules::IdentifyPattern({
        C(rules::Rank::Eight), C(rules::Rank::Eight, rules::Suit::Hearts), C(rules::Rank::Eight, rules::Suit::Diamonds),
        C(rules::Rank::Four)
    });
    CHECK_FALSE(tripleOne.pattern.IsValid());

    auto triplePair = rules::IdentifyPattern({
        C(rules::Rank::Nine), C(rules::Rank::Nine, rules::Suit::Hearts), C(rules::Rank::Nine, rules::Suit::Diamonds),
        C(rules::Rank::Five), C(rules::Rank::Five, rules::Suit::Hearts)
    });
    CHECK(triplePair.pattern.type == rules::PatternType::TripleWithPair);

    auto tripleTwoLooseCards = rules::IdentifyPattern({
        C(rules::Rank::Jack), C(rules::Rank::Jack, rules::Suit::Hearts), C(rules::Rank::Jack, rules::Suit::Diamonds),
        C(rules::Rank::Four), C(rules::Rank::Seven)
    });
    CHECK(tripleTwoLooseCards.pattern.type == rules::PatternType::TripleWithPair);

    auto planeMinWings = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts), C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Five), C(rules::Rank::Six)
    });
    CHECK(planeMinWings.pattern.type == rules::PatternType::Plane);
    CHECK(planeMinWings.pattern.groupCount == 2);
    CHECK(planeMinWings.pattern.mainRank == rules::Rank::Four);

    auto planeMaxWings = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts), C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Five), C(rules::Rank::Six), C(rules::Rank::Seven), C(rules::Rank::Eight)
    });
    CHECK(planeMaxWings.pattern.type == rules::PatternType::Plane);

    auto planeSplitBombs = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts), C(rules::Rank::Three, rules::Suit::Diamonds), C(rules::Rank::Three, rules::Suit::Clubs),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds), C(rules::Rank::Four, rules::Suit::Clubs),
        C(rules::Rank::Five), C(rules::Rank::Six)
    });
    CHECK(planeSplitBombs.pattern.type == rules::PatternType::Plane);

    CHECK_FALSE(rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts), C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Five)
    }).pattern.IsValid());

    CHECK_FALSE(rules::IdentifyPattern({
        C(rules::Rank::Ace), C(rules::Rank::Ace, rules::Suit::Hearts), C(rules::Rank::Ace, rules::Suit::Diamonds),
        C(rules::Rank::Two), C(rules::Rank::Two, rules::Suit::Hearts), C(rules::Rank::Two, rules::Suit::Diamonds),
        C(rules::Rank::Five), C(rules::Rank::Six)
    }).pattern.IsValid());

    CHECK_FALSE(rules::IdentifyPattern({
        C(rules::Rank::Nine), C(rules::Rank::Nine, rules::Suit::Hearts), C(rules::Rank::Nine, rules::Suit::Diamonds)
    }).pattern.IsValid());
    CHECK(rules::IdentifyPattern({
        C(rules::Rank::Nine), C(rules::Rank::Nine, rules::Suit::Hearts), C(rules::Rank::Nine, rules::Suit::Diamonds)
    }, 3, true).pattern.lastHandShort);
}

TEST_CASE("bombs cannot be played as four with three") {
    auto bomb = rules::IdentifyPattern({
        C(rules::Rank::King), C(rules::Rank::King, rules::Suit::Hearts),
        C(rules::Rank::King, rules::Suit::Diamonds), C(rules::Rank::King, rules::Suit::Clubs)
    });
    CHECK(bomb.pattern.type == rules::PatternType::Bomb);

    auto aceBomb = rules::IdentifyPattern({
        C(rules::Rank::Ace), C(rules::Rank::Ace, rules::Suit::Hearts),
        C(rules::Rank::Ace, rules::Suit::Diamonds), C(rules::Rank::Ace, rules::Suit::Clubs)
    });
    CHECK_FALSE(aceBomb.pattern.IsValid());

    auto fourWithThree = rules::IdentifyPattern({
        C(rules::Rank::Six), C(rules::Rank::Six, rules::Suit::Hearts),
        C(rules::Rank::Six, rules::Suit::Diamonds), C(rules::Rank::Six, rules::Suit::Clubs),
        C(rules::Rank::Three), C(rules::Rank::Four), C(rules::Rank::Five)
    });
    CHECK_FALSE(fourWithThree.pattern.IsValid());
}

TEST_CASE("move comparison follows fixed rules") {
    const auto pair5 = rules::IdentifyPattern({C(rules::Rank::Five), C(rules::Rank::Five, rules::Suit::Hearts)}).pattern;
    const auto pair6 = rules::IdentifyPattern({C(rules::Rank::Six), C(rules::Rank::Six, rules::Suit::Hearts)}).pattern;
    CHECK(rules::CanBeat(pair6, pair5));
    CHECK_FALSE(rules::CanBeat(pair5, pair6));

    const auto straightLow = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Four), C(rules::Rank::Five), C(rules::Rank::Six), C(rules::Rank::Seven)
    }).pattern;
    const auto straightHigh = rules::IdentifyPattern({
        C(rules::Rank::Four), C(rules::Rank::Five), C(rules::Rank::Six), C(rules::Rank::Seven), C(rules::Rank::Eight)
    }).pattern;
    CHECK(rules::CanBeat(straightHigh, straightLow));

    const auto singleAce = rules::IdentifyPattern({C(rules::Rank::Ace)}).pattern;
    const auto bomb3 = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts),
        C(rules::Rank::Three, rules::Suit::Diamonds), C(rules::Rank::Three, rules::Suit::Clubs)
    }).pattern;
    CHECK(rules::CanBeat(bomb3, singleAce));

    const auto planeLow = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts), C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Queen), C(rules::Rank::King)
    }).pattern;
    const auto planeHigh = rules::IdentifyPattern({
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Five), C(rules::Rank::Five, rules::Suit::Hearts), C(rules::Rank::Five, rules::Suit::Diamonds),
        C(rules::Rank::Six), C(rules::Rank::Seven)
    }).pattern;
    const auto planeThreeGroups = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts), C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Five), C(rules::Rank::Five, rules::Suit::Hearts), C(rules::Rank::Five, rules::Suit::Diamonds),
        C(rules::Rank::Six), C(rules::Rank::Seven), C(rules::Rank::Eight)
    }).pattern;
    CHECK(rules::CanBeat(planeHigh, planeLow));
    CHECK_FALSE(rules::CanBeat(planeLow, planeHigh));
    CHECK_FALSE(rules::CanBeat(planeHigh, planeThreeGroups));
    CHECK_FALSE(rules::CanBeat(planeThreeGroups, planeHigh));

    const auto tripleTwo = rules::IdentifyPattern({
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts),
        C(rules::Rank::Four, rules::Suit::Diamonds), C(rules::Rank::Five), C(rules::Rank::Six)
    }).pattern;
    CHECK(tripleTwo.IsValid());
}

TEST_CASE("lead validation allows short final triples and plane but follow validation does not") {
    const rules::Cards bareTriple{
        C(rules::Rank::Nine), C(rules::Rank::Nine, rules::Suit::Hearts), C(rules::Rank::Nine, rules::Suit::Diamonds)
    };
    const auto bareTripleLead = rules::ValidateLead(bareTriple, static_cast<int>(bareTriple.size()));
    CHECK(bareTripleLead.ok);
    CHECK(bareTripleLead.pattern.type == rules::PatternType::TripleWithOne);
    CHECK(bareTripleLead.pattern.lastHandShort);

    const rules::Cards tripleWithOne{
        C(rules::Rank::Nine), C(rules::Rank::Nine, rules::Suit::Hearts), C(rules::Rank::Nine, rules::Suit::Diamonds),
        C(rules::Rank::Four)
    };
    const auto tripleWithOneNormal = rules::ValidateLead(tripleWithOne, 5);
    CHECK_FALSE(tripleWithOneNormal.ok);

    const auto tripleWithOneFinal = rules::ValidateLead(tripleWithOne, static_cast<int>(tripleWithOne.size()));
    CHECK(tripleWithOneFinal.ok);
    CHECK(tripleWithOneFinal.pattern.type == rules::PatternType::TripleWithOne);
    CHECK(tripleWithOneFinal.pattern.lastHandShort);

    const auto previousTripleWithPair = rules::IdentifyPattern({
        C(rules::Rank::Eight), C(rules::Rank::Eight, rules::Suit::Hearts), C(rules::Rank::Eight, rules::Suit::Diamonds),
        C(rules::Rank::Five), C(rules::Rank::Six)
    }).pattern;
    const auto tripleWithOneFollow = rules::ValidateFollow(tripleWithOne, previousTripleWithPair, static_cast<int>(tripleWithOne.size()));
    CHECK_FALSE(tripleWithOneFollow.ok);

    const rules::Cards shortPlane{
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts), C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds)
    };
    const auto lead = rules::ValidateLead(shortPlane, static_cast<int>(shortPlane.size()));
    CHECK(lead.ok);
    CHECK(lead.pattern.type == rules::PatternType::Plane);
    CHECK(lead.pattern.groupCount == 2);
    CHECK(lead.pattern.lastHandShort);

    const auto previous = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts), C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Five), C(rules::Rank::Six)
    }).pattern;
    const auto follow = rules::ValidateFollow(shortPlane, previous, static_cast<int>(shortPlane.size()));
    CHECK_FALSE(follow.ok);
}

TEST_CASE("follow existence check uses rules without ai scoring") {
    const auto singleFour = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    CHECK(rules::HasAnyFollowMove({C(rules::Rank::Five)}, singleFour, 1));
    CHECK_FALSE(rules::HasAnyFollowMove({C(rules::Rank::Three)}, singleFour, 1));

    const auto singleAce = rules::IdentifyPattern({C(rules::Rank::Ace)}).pattern;
    CHECK(rules::HasAnyFollowMove({
        C(rules::Rank::Three),
        C(rules::Rank::Three, rules::Suit::Hearts),
        C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Three, rules::Suit::Clubs)
    }, singleAce, 4));

    const auto straightSix = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Four), C(rules::Rank::Five),
        C(rules::Rank::Six), C(rules::Rank::Seven), C(rules::Rank::Eight)
    }).pattern;
    CHECK_FALSE(rules::HasAnyFollowMove({
        C(rules::Rank::Ten), C(rules::Rank::Jack), C(rules::Rank::Queen), C(rules::Rank::King), C(rules::Rank::Ace)
    }, straightSix, 5));

    const auto planeThreeGroups = rules::IdentifyPattern({
        C(rules::Rank::Three), C(rules::Rank::Three, rules::Suit::Hearts), C(rules::Rank::Three, rules::Suit::Diamonds),
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Five), C(rules::Rank::Five, rules::Suit::Hearts), C(rules::Rank::Five, rules::Suit::Diamonds),
        C(rules::Rank::Six), C(rules::Rank::Seven), C(rules::Rank::Eight)
    }).pattern;
    CHECK_FALSE(rules::HasAnyFollowMove({
        C(rules::Rank::Four), C(rules::Rank::Four, rules::Suit::Hearts), C(rules::Rank::Four, rules::Suit::Diamonds),
        C(rules::Rank::Five), C(rules::Rank::Five, rules::Suit::Hearts), C(rules::Rank::Five, rules::Suit::Diamonds),
        C(rules::Rank::Seven), C(rules::Rank::Eight)
    }, planeThreeGroups, 8));
}
