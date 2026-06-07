#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include <doctest/doctest.h>

#include "rules/Deck.h"
#include "rules/MoveValidator.h"
#include "rules/PaoDeKuaiRules.h"
#include "rules/Scoring.h"
#include "game/AiStrategy.h"
#include "game/GameState.h"
#include "stats/AppSettings.h"
#include "stats/StatStore.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <random>

using namespace pdk;

namespace {

rules::Card C(rules::Rank rank, rules::Suit suit = rules::Suit::Spades) {
    return {rank, suit};
}

int CountRank(const rules::Cards& cards, rules::Rank rank) {
    return static_cast<int>(std::count_if(cards.begin(), cards.end(), [rank](rules::Card card) {
        return card.rank == rank;
    }));
}

game::AiContext LeadContext(int ownRemaining, int nextRemaining = 16, int minOpponentRemaining = 16) {
    game::AiContext context;
    context.leading = true;
    context.ownRemainingCards = ownRemaining;
    context.nextPlayerRemainingCards = nextRemaining;
    context.minOpponentRemainingCards = minOpponentRemaining;
    context.remainingCards = {ownRemaining, nextRemaining, minOpponentRemaining};
    return context;
}

game::AiContext FollowContext(const rules::HandPattern& previous, int ownRemaining = 16) {
    game::AiContext context;
    context.leading = false;
    context.previous = previous;
    context.ownRemainingCards = ownRemaining;
    context.nextPlayerRemainingCards = 16;
    context.minOpponentRemainingCards = 16;
    context.remainingCards = {ownRemaining, 16, 16};
    return context;
}

} // namespace

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
    CHECK(tripleOne.pattern.type == rules::PatternType::TripleWithOne);

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
    }, 3).pattern.lastHandShort);
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
}

TEST_CASE("lead validation allows short final plane but follow validation does not") {
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

TEST_CASE("scoring examples and bomb fixed points") {
    rules::RoundScoreInput input;
    input.winner = rules::PlayerId::Player;
    input.remainingCards = {0, 8, 1};
    input.hasPlayedCards = {true, true, true};
    auto score = rules::CalculateRoundScore(input);
    CHECK(score.scores == std::array<int, 3>{8, -8, 0});

    input.remainingCards = {0, 16, 5};
    input.hasPlayedCards = {true, false, true};
    score = rules::CalculateRoundScore(input);
    CHECK(score.scores == std::array<int, 3>{37, -32, -5});

    input.remainingCards = {0, 16, 16};
    input.hasPlayedCards = {true, false, false};
    score = rules::CalculateRoundScore(input);
    CHECK(score.scores == std::array<int, 3>{64, -32, -32});
    CHECK(score.spring.enabled);

    input.remainingCards = {0, 8, 10};
    input.hasPlayedCards = {true, true, true};
    input.bombs = {rules::BombScoreEvent{rules::PlayerId::Player, 20}};
    score = rules::CalculateRoundScore(input);
    CHECK(score.scores == std::array<int, 3>{38, -18, -20});
}

TEST_CASE("game state can finish a full three player autoplay round") {
    game::GameState state;
    state.StartNewRound("Tester", 20260606u);
    state.ToggleAutoplay();

    for (int i = 0; i < 600 && !state.IsRoundOver(); ++i) {
        state.Update(0.5f);
        state.ClearEvents();
    }

    CHECK(state.IsRoundOver());
    const stats::RoundRecord& record = state.LastRoundRecord();
    CHECK((record.winner == rules::PlayerId::Player ||
           record.winner == rules::PlayerId::Ai1 ||
           record.winner == rules::PlayerId::Ai2));
    CHECK(record.remainingCards[rules::PlayerIndex(record.winner)] == 0);
    CHECK(record.scores[0] + record.scores[1] + record.scores[2] == 0);
}

TEST_CASE("turn order is counterclockwise so the left-hand player is upstream") {
    game::GameState state;
    bool foundAi2Start = false;
    for (unsigned seed = 1; seed < 300 && !foundAi2Start; ++seed) {
        state.StartNewRound("Tester", seed);
        if (state.CurrentPlayer() == rules::PlayerId::Ai2) {
            foundAi2Start = true;
        }
    }
    REQUIRE(foundAi2Start);

    state.Update(0.2f);
    CHECK(state.CurrentPlayer() == rules::PlayerId::Ai2);
    state.Update(1.0f);
    CHECK(state.CurrentPlayer() == rules::PlayerId::Ai1);
}

TEST_CASE("hint passes directly when player cannot beat and pass is blocked when player can beat") {
    const auto aceLead = rules::IdentifyPattern({C(rules::Rank::Ace)}).pattern;
    game::GameState cannotBeat;
    cannotBeat.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::King)},
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Four)}
        },
        rules::PlayerId::Player,
        aceLead,
        rules::PlayerId::Ai1);

    CHECK(cannotBeat.CanCurrentPlayerPass());
    CHECK(cannotBeat.ApplyHint());
    REQUIRE_FALSE(cannotBeat.Events().empty());
    CHECK(cannotBeat.Events().back().type == game::GameEventType::Passed);
    CHECK(cannotBeat.CurrentPlayer() == rules::PlayerId::Ai2);

    const auto fourLead = rules::IdentifyPattern({C(rules::Rank::Four)}).pattern;
    game::GameState canBeat;
    canBeat.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{C(rules::Rank::Five)},
            rules::Cards{C(rules::Rank::Three)},
            rules::Cards{C(rules::Rank::Six)}
        },
        rules::PlayerId::Player,
        fourLead,
        rules::PlayerId::Ai1);

    CHECK_FALSE(canBeat.CanCurrentPlayerPass());
    CHECK_FALSE(canBeat.PassHuman());
    REQUIRE_FALSE(canBeat.Events().empty());
    CHECK(canBeat.Events().back().type == game::GameEventType::InvalidMove);
}

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

TEST_CASE("drag selection picks best lead pattern from dragged cards and ignores previous move") {
    game::GameState state;
    const auto previousStraight = rules::IdentifyPattern({
        C(rules::Rank::Ten),
        C(rules::Rank::Jack),
        C(rules::Rank::Queen),
        C(rules::Rank::King),
        C(rules::Rank::Ace)
    }).pattern;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{
                C(rules::Rank::Three),
                C(rules::Rank::Four),
                C(rules::Rank::Five),
                C(rules::Rank::Six),
                C(rules::Rank::Seven),
                C(rules::Rank::Nine),
                C(rules::Rank::Nine, rules::Suit::Hearts)
            },
            rules::Cards{C(rules::Rank::Ace)},
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Player,
        previousStraight,
        rules::PlayerId::Ai1);

    CHECK(state.SelectBestPatternFromDraggedCards({6, 5, 4, 3, 2, 1, 0}));
    CHECK(state.SelectedIndices().size() == 5);
    CHECK(state.SelectedIndices().contains(0));
    CHECK(state.SelectedIndices().contains(4));
}

TEST_CASE("drag selection chooses four dragged bomb cards before a smaller pair") {
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{
                C(rules::Rank::Three),
                C(rules::Rank::Three, rules::Suit::Hearts),
                C(rules::Rank::Three, rules::Suit::Diamonds),
                C(rules::Rank::Three, rules::Suit::Clubs),
                C(rules::Rank::Nine),
                C(rules::Rank::Nine, rules::Suit::Hearts)
            },
            rules::Cards{C(rules::Rank::Ace)},
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Player,
        std::nullopt,
        rules::PlayerId::Ai1);

    CHECK(state.SelectBestPatternFromDraggedCards({0, 1, 2, 3}));
    CHECK(state.SelectedIndices().size() == 4);
    CHECK(state.SelectedIndices().contains(0));
    CHECK(state.SelectedIndices().contains(1));
    CHECK(state.SelectedIndices().contains(2));
    CHECK(state.SelectedIndices().contains(3));
    CHECK_FALSE(state.SelectedIndices().contains(4));
    CHECK_FALSE(state.SelectedIndices().contains(5));
}

TEST_CASE("drag selection can choose the longest plane from dragged cards") {
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{
                C(rules::Rank::Three),
                C(rules::Rank::Three, rules::Suit::Hearts),
                C(rules::Rank::Three, rules::Suit::Diamonds),
                C(rules::Rank::Four),
                C(rules::Rank::Four, rules::Suit::Hearts),
                C(rules::Rank::Four, rules::Suit::Diamonds),
                C(rules::Rank::Five),
                C(rules::Rank::Six),
                C(rules::Rank::Seven),
                C(rules::Rank::Eight),
                C(rules::Rank::Nine),
                C(rules::Rank::Nine, rules::Suit::Hearts)
            },
            rules::Cards{C(rules::Rank::Ace)},
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Player,
        std::nullopt,
        rules::PlayerId::Ai1);

    CHECK(state.SelectBestPatternFromDraggedCards({0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11}));
    CHECK(state.SelectedIndices().size() == 10);
    for (int i = 0; i < 10; ++i) {
        CHECK(state.SelectedIndices().contains(i));
    }
    CHECK_FALSE(state.SelectedIndices().contains(10));
    CHECK_FALSE(state.SelectedIndices().contains(11));
}

TEST_CASE("settings and daily stats use current working directory style json") {
    const auto root = std::filesystem::temp_directory_path() / "pao_de_kuai_rules_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    stats::AppSettings settings;
    settings.playerName = "Tester";
    settings.masterVolume = 0.5f;
    const auto settingsPath = (root / "appsettings.json").string();
    CHECK(stats::SaveAppSettings(settings, settingsPath));
    const stats::AppSettings loaded = stats::LoadAppSettings(settingsPath);
    CHECK(loaded.playerName == "Tester");
    CHECK(loaded.masterVolume == doctest::Approx(0.5));
    std::ifstream settingsFile(settingsPath, std::ios::binary);
    const std::string settingsJson((std::istreambuf_iterator<char>(settingsFile)), std::istreambuf_iterator<char>());
    settingsFile.close();
    CHECK(settingsJson.find("cardScale") == std::string::npos);
    CHECK(settingsJson.find("animationSpeed") == std::string::npos);

    stats::StatStore store(root);
    stats::RoundRecord round;
    round.startedAt = "20:12:31";
    round.endedAt = "20:16:02";
    round.winner = rules::PlayerId::Player;
    round.playerName = "Tester";
    round.scores = {18, -8, -10};
    round.remainingCards = {0, 8, 10};
    round.bombs = {rules::BombScoreEvent{rules::PlayerId::Player, 20}};
    CHECK(store.AppendRound("20260606", round));

    const stats::StatSummary day = store.SummarizeDay("20260606");
    CHECK(day.rounds == 1);
    CHECK(day.scores[0] == 18);
    CHECK(day.bombs == 1);
    const stats::StatSummary month = store.SummarizeMonth("202606");
    CHECK(month.rounds == 1);
    const stats::StatSummary history = store.SummarizeHistory();
    CHECK(history.rounds == 1);

    std::filesystem::remove_all(root);
}
