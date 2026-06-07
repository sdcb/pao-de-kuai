#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "game/GameState.h"

using namespace pdk;
using tests::C;

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

TEST_CASE("drag selection can choose triple and plane cores without kickers") {
    game::GameState tripleState;
    tripleState.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{
                C(rules::Rank::Three),
                C(rules::Rank::Three, rules::Suit::Hearts),
                C(rules::Rank::Three, rules::Suit::Diamonds),
                C(rules::Rank::Nine)
            },
            rules::Cards{C(rules::Rank::Ace)},
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Player,
        std::nullopt,
        rules::PlayerId::Ai1);

    CHECK(tripleState.SelectBestPatternFromDraggedCards({0, 1, 2}));
    CHECK(tripleState.SelectedIndices().size() == 3);
    CHECK(tripleState.SelectedIndices().contains(0));
    CHECK(tripleState.SelectedIndices().contains(1));
    CHECK(tripleState.SelectedIndices().contains(2));

    game::GameState planeState;
    planeState.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{
                C(rules::Rank::Three),
                C(rules::Rank::Three, rules::Suit::Hearts),
                C(rules::Rank::Three, rules::Suit::Diamonds),
                C(rules::Rank::Four),
                C(rules::Rank::Four, rules::Suit::Hearts),
                C(rules::Rank::Four, rules::Suit::Diamonds),
                C(rules::Rank::Nine)
            },
            rules::Cards{C(rules::Rank::Ace)},
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Player,
        std::nullopt,
        rules::PlayerId::Ai1);

    CHECK(planeState.SelectBestPatternFromDraggedCards({0, 1, 2, 3, 4, 5}));
    CHECK(planeState.SelectedIndices().size() == 6);
    for (int i = 0; i < 6; ++i) {
        CHECK(planeState.SelectedIndices().contains(i));
    }
    CHECK_FALSE(planeState.SelectedIndices().contains(6));
}

TEST_CASE("drag selection toggles off when the chosen group is already selected") {
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::Cards{
                C(rules::Rank::Three),
                C(rules::Rank::Three, rules::Suit::Hearts),
                C(rules::Rank::Three, rules::Suit::Diamonds),
                C(rules::Rank::Nine)
            },
            rules::Cards{C(rules::Rank::Ace)},
            rules::Cards{C(rules::Rank::King)}
        },
        rules::PlayerId::Player,
        std::nullopt,
        rules::PlayerId::Ai1);

    REQUIRE(state.SelectBestPatternFromDraggedCards({0, 1, 2}));
    REQUIRE(state.SelectedIndices().size() == 3);

    REQUIRE(state.SelectBestPatternFromDraggedCards({0, 1, 2}));
    CHECK(state.SelectedIndices().empty());
    CHECK(state.HintIndices().empty());
}
