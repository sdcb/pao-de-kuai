#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "rules/CppCompat.h"

#include <array>
#include <random>
#include <vector>

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
        MakeCards({C(RANK_FOUR)}),
        MakeCards({C(RANK_THREE, SUIT_SPADES)}),
        MakeCards({C(RANK_ACE)})
    };
    CHECK(rules::FindFirstPlayerBySpadeThree(hands) == 1);
}

TEST_CASE("recognizes core hand patterns") {
    CHECK(rules::IdentifyPattern(MakeCards({C(RANK_FIVE)})).pattern.type == PATTERN_SINGLE);
    CHECK(rules::IdentifyPattern(MakeCards({C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS)})).pattern.type == PATTERN_PAIR);

    auto straight = rules::IdentifyPattern(MakeCards({
        C(RANK_TEN), C(RANK_JACK), C(RANK_QUEEN), C(RANK_KING), C(RANK_ACE)
    }));
    CHECK(straight.pattern.type == PATTERN_STRAIGHT);

    auto jqka2 = rules::IdentifyPattern(MakeCards({
        C(RANK_JACK), C(RANK_QUEEN), C(RANK_KING), C(RANK_ACE), C(RANK_TWO)
    }));
    CHECK_FALSE(rules::IsValid(jqka2.pattern));

    auto twoPairs = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS)
    }));
    CHECK(twoPairs.pattern.type == PATTERN_CONSECUTIVE_PAIRS);

    auto pairs = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS)
    }));
    CHECK(pairs.pattern.type == PATTERN_CONSECUTIVE_PAIRS);

    auto tripleOne = rules::IdentifyPattern(MakeCards({
        C(RANK_EIGHT), C(RANK_EIGHT, SUIT_HEARTS), C(RANK_EIGHT, SUIT_DIAMONDS),
        C(RANK_FOUR)
    }));
    CHECK_FALSE(rules::IsValid(tripleOne.pattern));

    auto triplePair = rules::IdentifyPattern(MakeCards({
        C(RANK_NINE), C(RANK_NINE, SUIT_HEARTS), C(RANK_NINE, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS)
    }));
    CHECK(triplePair.pattern.type == PATTERN_TRIPLE_WITH_PAIR);

    auto tripleTwoLooseCards = rules::IdentifyPattern(MakeCards({
        C(RANK_JACK), C(RANK_JACK, SUIT_HEARTS), C(RANK_JACK, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_SEVEN)
    }));
    CHECK(tripleTwoLooseCards.pattern.type == PATTERN_TRIPLE_WITH_PAIR);

    auto planeMinWings = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_SIX)
    }));
    CHECK(planeMinWings.pattern.type == PATTERN_PLANE);
    CHECK(planeMinWings.pattern.groupCount == 2);
    CHECK(planeMinWings.pattern.mainRank == RANK_FOUR);

    auto planeMaxWings = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_SIX), C(RANK_SEVEN), C(RANK_EIGHT)
    }));
    CHECK(planeMaxWings.pattern.type == PATTERN_PLANE);

    auto planeSplitBombs = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS), C(RANK_THREE, SUIT_CLUBS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS), C(RANK_FOUR, SUIT_CLUBS),
        C(RANK_FIVE), C(RANK_SIX)
    }));
    CHECK(planeSplitBombs.pattern.type == PATTERN_PLANE);

    const rules::PatternResult fourWithExtra = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE)
    }));
    CHECK_FALSE(rules::IsValid(fourWithExtra.pattern));

    const rules::PatternResult mixedRun = rules::IdentifyPattern(MakeCards({
        C(RANK_ACE), C(RANK_ACE, SUIT_HEARTS), C(RANK_ACE, SUIT_DIAMONDS),
        C(RANK_TWO), C(RANK_TWO, SUIT_HEARTS), C(RANK_TWO, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_SIX)
    }));
    CHECK_FALSE(rules::IsValid(mixedRun.pattern));

    const rules::PatternResult bareTriple = rules::IdentifyPattern(MakeCards({
        C(RANK_NINE), C(RANK_NINE, SUIT_HEARTS), C(RANK_NINE, SUIT_DIAMONDS)
    }));
    CHECK_FALSE(rules::IsValid(bareTriple.pattern));
    CHECK(rules::IdentifyPattern(MakeCards({
        C(RANK_NINE), C(RANK_NINE, SUIT_HEARTS), C(RANK_NINE, SUIT_DIAMONDS)
    }), 3, true).pattern.lastHandShort);
}

TEST_CASE("bombs cannot be played as four with three") {
    auto bomb = rules::IdentifyPattern(MakeCards({
        C(RANK_KING), C(RANK_KING, SUIT_HEARTS),
        C(RANK_KING, SUIT_DIAMONDS), C(RANK_KING, SUIT_CLUBS)
    }));
    CHECK(bomb.pattern.type == PATTERN_BOMB);

    auto aceBomb = rules::IdentifyPattern(MakeCards({
        C(RANK_ACE), C(RANK_ACE, SUIT_HEARTS),
        C(RANK_ACE, SUIT_DIAMONDS), C(RANK_ACE, SUIT_CLUBS)
    }));
    CHECK_FALSE(rules::IsValid(aceBomb.pattern));

    auto fourWithThree = rules::IdentifyPattern(MakeCards({
        C(RANK_SIX), C(RANK_SIX, SUIT_HEARTS),
        C(RANK_SIX, SUIT_DIAMONDS), C(RANK_SIX, SUIT_CLUBS),
        C(RANK_THREE), C(RANK_FOUR), C(RANK_FIVE)
    }));
    CHECK_FALSE(rules::IsValid(fourWithThree.pattern));
}

TEST_CASE("move comparison follows fixed rules") {
    const auto pair5 = rules::IdentifyPattern(MakeCards({C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS)})).pattern;
    const auto pair6 = rules::IdentifyPattern(MakeCards({C(RANK_SIX), C(RANK_SIX, SUIT_HEARTS)})).pattern;
    CHECK(rules::CanBeat(pair6, pair5));
    CHECK_FALSE(rules::CanBeat(pair5, pair6));

    const auto straightLow = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_FOUR), C(RANK_FIVE), C(RANK_SIX), C(RANK_SEVEN)
    })).pattern;
    const auto straightHigh = rules::IdentifyPattern(MakeCards({
        C(RANK_FOUR), C(RANK_FIVE), C(RANK_SIX), C(RANK_SEVEN), C(RANK_EIGHT)
    })).pattern;
    CHECK(rules::CanBeat(straightHigh, straightLow));

    const auto singleAce = rules::IdentifyPattern(MakeCards({C(RANK_ACE)})).pattern;
    const auto bomb3 = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS),
        C(RANK_THREE, SUIT_DIAMONDS), C(RANK_THREE, SUIT_CLUBS)
    })).pattern;
    CHECK(rules::CanBeat(bomb3, singleAce));

    const auto planeLow = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_QUEEN), C(RANK_KING)
    })).pattern;
    const auto planeHigh = rules::IdentifyPattern(MakeCards({
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS), C(RANK_FIVE, SUIT_DIAMONDS),
        C(RANK_SIX), C(RANK_SEVEN)
    })).pattern;
    const auto planeThreeGroups = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS), C(RANK_FIVE, SUIT_DIAMONDS),
        C(RANK_SIX), C(RANK_SEVEN), C(RANK_EIGHT)
    })).pattern;
    CHECK(rules::CanBeat(planeHigh, planeLow));
    CHECK_FALSE(rules::CanBeat(planeLow, planeHigh));
    CHECK_FALSE(rules::CanBeat(planeHigh, planeThreeGroups));
    CHECK_FALSE(rules::CanBeat(planeThreeGroups, planeHigh));

    const auto tripleTwo = rules::IdentifyPattern(MakeCards({
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS),
        C(RANK_FOUR, SUIT_DIAMONDS), C(RANK_FIVE), C(RANK_SIX)
    })).pattern;
    CHECK(rules::IsValid(tripleTwo));
}

TEST_CASE("lead validation allows short final triples and plane but follow validation does not") {
    const rules::Cards bareTriple = MakeCards({
        C(RANK_NINE), C(RANK_NINE, SUIT_HEARTS), C(RANK_NINE, SUIT_DIAMONDS)
    });
    const auto bareTripleLead = rules::ValidateLead(bareTriple, static_cast<int>(bareTriple.size()));
    CHECK(bareTripleLead.ok);
    CHECK(bareTripleLead.pattern.type == PATTERN_TRIPLE_WITH_ONE);
    CHECK(bareTripleLead.pattern.lastHandShort);

    const rules::Cards tripleWithOne = MakeCards({
        C(RANK_NINE), C(RANK_NINE, SUIT_HEARTS), C(RANK_NINE, SUIT_DIAMONDS),
        C(RANK_FOUR)
    });
    const auto tripleWithOneNormal = rules::ValidateLead(tripleWithOne, 5);
    CHECK_FALSE(tripleWithOneNormal.ok);

    const auto tripleWithOneFinal = rules::ValidateLead(tripleWithOne, static_cast<int>(tripleWithOne.size()));
    CHECK(tripleWithOneFinal.ok);
    CHECK(tripleWithOneFinal.pattern.type == PATTERN_TRIPLE_WITH_ONE);
    CHECK(tripleWithOneFinal.pattern.lastHandShort);

    const auto previousTripleWithPair = rules::IdentifyPattern(MakeCards({
        C(RANK_EIGHT), C(RANK_EIGHT, SUIT_HEARTS), C(RANK_EIGHT, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_SIX)
    })).pattern;
    const auto tripleWithOneFollow = rules::ValidateFollow(tripleWithOne, previousTripleWithPair, static_cast<int>(tripleWithOne.size()));
    CHECK_FALSE(tripleWithOneFollow.ok);

    const rules::Cards shortPlane = MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS)
    });
    const auto lead = rules::ValidateLead(shortPlane, static_cast<int>(shortPlane.size()));
    CHECK(lead.ok);
    CHECK(lead.pattern.type == PATTERN_PLANE);
    CHECK(lead.pattern.groupCount == 2);
    CHECK(lead.pattern.lastHandShort);

    const auto previous = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_SIX)
    })).pattern;
    const auto follow = rules::ValidateFollow(shortPlane, previous, static_cast<int>(shortPlane.size()));
    CHECK_FALSE(follow.ok);
}

TEST_CASE("follow existence check uses rules without ai scoring") {
    const auto singleFour = rules::IdentifyPattern(MakeCards({C(RANK_FOUR)})).pattern;
    CHECK(rules::HasAnyFollowMove(MakeCards({C(RANK_FIVE)}), singleFour, 1));
    CHECK_FALSE(rules::HasAnyFollowMove(MakeCards({C(RANK_THREE)}), singleFour, 1));

    const auto singleAce = rules::IdentifyPattern(MakeCards({C(RANK_ACE)})).pattern;
    CHECK(rules::HasAnyFollowMove(MakeCards({
        C(RANK_THREE),
        C(RANK_THREE, SUIT_HEARTS),
        C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_THREE, SUIT_CLUBS)
    }), singleAce, 4));

    const auto straightSix = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_FOUR), C(RANK_FIVE),
        C(RANK_SIX), C(RANK_SEVEN), C(RANK_EIGHT)
    })).pattern;
    CHECK_FALSE(rules::HasAnyFollowMove(MakeCards({
        C(RANK_TEN), C(RANK_JACK), C(RANK_QUEEN), C(RANK_KING), C(RANK_ACE)
    }), straightSix, 5));

    const auto planeThreeGroups = rules::IdentifyPattern(MakeCards({
        C(RANK_THREE), C(RANK_THREE, SUIT_HEARTS), C(RANK_THREE, SUIT_DIAMONDS),
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS), C(RANK_FIVE, SUIT_DIAMONDS),
        C(RANK_SIX), C(RANK_SEVEN), C(RANK_EIGHT)
    })).pattern;
    CHECK_FALSE(rules::HasAnyFollowMove(MakeCards({
        C(RANK_FOUR), C(RANK_FOUR, SUIT_HEARTS), C(RANK_FOUR, SUIT_DIAMONDS),
        C(RANK_FIVE), C(RANK_FIVE, SUIT_HEARTS), C(RANK_FIVE, SUIT_DIAMONDS),
        C(RANK_SEVEN), C(RANK_EIGHT)
    }), planeThreeGroups, 8));
}

TEST_CASE("rank count follow check agrees with the exhaustive subset search") {
    // HasAnyFollowMove answers from rank counts; HasAnyFollowMoveExhaustive is the
    // original subset walk through ValidateFollow.  Every AI search depends on the two
    // agreeing, so compare them on random and deliberately clustered hands against
    // patterns cut from other random hands.
    Cards sortedDeck = rules::CreatePaoDeKuaiDeck();
    std::sort(sortedDeck.items, sortedDeck.items + sortedDeck.count,
        [](Card lhs, Card rhs) { return lhs.rank < rhs.rank; });
    std::mt19937 rng(20261019u);
    std::array<int, PATTERN_BOMB + 1> covered{};
    int checks = 0;
    int positives = 0;

    auto randomHand = [&](int size, bool clustered) {
        Cards source = sortedDeck;
        if (clustered) {
            // A window of neighbouring ranks makes triples, bombs, runs and planes common.
            const int window = 26;
            const int offset = std::uniform_int_distribution<int>(0, source.count - window)(rng);
            std::shuffle(source.items + offset, source.items + offset + window, rng);
            Cards hand;
            Cards_Clear(&hand);
            for (int i = 0; i < size && i < window; ++i) {
                Cards_Push(&hand, source.items[offset + i]);
            }
            return hand;
        }
        std::shuffle(source.items, source.items + source.count, rng);
        Cards hand;
        Cards_Clear(&hand);
        for (int i = 0; i < size; ++i) {
            Cards_Push(&hand, source.items[i]);
        }
        return hand;
    };

    for (int deal = 0; deal < 400; ++deal) {
        // Collect the distinct legal leads of a 10-card hand, short-final ones included.
        const Cards lead = randomHand(10, deal % 2 == 0);
        std::vector<HandPattern> patterns;
        for (int mask = 1; mask < (1 << lead.count); ++mask) {
            Cards play;
            Cards_Clear(&play);
            for (int i = 0; i < lead.count; ++i) {
                if ((mask & (1 << i)) != 0) {
                    Cards_Push(&play, lead.items[i]);
                }
            }
            const HandPattern pattern = ::IdentifyPattern(&play, play.count, true).pattern;
            if (!HandPattern_IsValid(&pattern)) {
                continue;
            }
            const bool seen = std::any_of(patterns.begin(), patterns.end(), [&](const HandPattern& p) {
                return p.type == pattern.type && p.mainRank == pattern.mainRank &&
                    p.cardCount == pattern.cardCount && p.groupCount == pattern.groupCount &&
                    p.lastHandShort == pattern.lastHandShort;
            });
            if (!seen) {
                patterns.push_back(pattern);
            }
        }
        std::shuffle(patterns.begin(), patterns.end(), rng);

        for (std::size_t p = 0; p < patterns.size() && p < 12; ++p) {
            const int size = std::uniform_int_distribution<int>(1, 16)(rng);
            const Cards hand = randomHand(size, (deal + static_cast<int>(p)) % 3 != 0);
            const bool fast = ::HasAnyFollowMove(&hand, &patterns[p], hand.count);
            const bool exhaustive = ::HasAnyFollowMoveExhaustive(&hand, &patterns[p], hand.count);
            INFO("deal=" << deal << " pattern type=" << static_cast<int>(patterns[p].type)
                << " rank=" << static_cast<int>(patterns[p].mainRank)
                << " cards=" << patterns[p].cardCount << " hand=" << hand.count);
            REQUIRE(fast == exhaustive);
            covered[patterns[p].type]++;
            positives += fast ? 1 : 0;
            checks++;
        }
    }

    for (int type = PATTERN_SINGLE; type <= PATTERN_BOMB; ++type) {
        INFO("pattern type " << type << " was never tested");
        CHECK(covered[static_cast<std::size_t>(type)] > 0);
    }
    CHECK(positives > checks / 10);
    CHECK(positives < checks - checks / 10);
}
