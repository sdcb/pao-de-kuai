#include <doctest/doctest.h>

#include "TestHelpers.h"
#include "game/GameState.h"

using namespace pdk;
using tests::C;

TEST_CASE("drag selection picks best lead pattern from dragged cards and ignores previous move") {
    game::GameState state;
    const auto previousStraight = rules::IdentifyPattern(MakeCards({
        C(RANK_TEN),
        C(RANK_JACK),
        C(RANK_QUEEN),
        C(RANK_KING),
        C(RANK_ACE)
    })).pattern;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::MakeCards({
                C(RANK_THREE),
                C(RANK_FOUR),
                C(RANK_FIVE),
                C(RANK_SIX),
                C(RANK_SEVEN),
                C(RANK_NINE),
                C(RANK_NINE, SUIT_HEARTS)
            }),
            rules::MakeCards({C(RANK_ACE)}),
            rules::MakeCards({C(RANK_KING)})
        },
        PLAYER_HUMAN,
        previousStraight,
        PLAYER_AI1);

    CHECK(state.SelectBestPatternFromDraggedCards({6, 5, 4, 3, 2, 1, 0}));
    CHECK(state.SelectedIndices().size() == 5);
    CHECK(state.SelectedIndices().contains(0));
    CHECK(state.SelectedIndices().contains(4));
}

TEST_CASE("drag selection chooses four dragged bomb cards before a smaller pair") {
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::MakeCards({
                C(RANK_THREE),
                C(RANK_THREE, SUIT_HEARTS),
                C(RANK_THREE, SUIT_DIAMONDS),
                C(RANK_THREE, SUIT_CLUBS),
                C(RANK_NINE),
                C(RANK_NINE, SUIT_HEARTS)
            }),
            rules::MakeCards({C(RANK_ACE)}),
            rules::MakeCards({C(RANK_KING)})
        },
        PLAYER_HUMAN,
        std::nullopt,
        PLAYER_AI1);

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
            rules::MakeCards({
                C(RANK_THREE),
                C(RANK_THREE, SUIT_HEARTS),
                C(RANK_THREE, SUIT_DIAMONDS),
                C(RANK_FOUR),
                C(RANK_FOUR, SUIT_HEARTS),
                C(RANK_FOUR, SUIT_DIAMONDS),
                C(RANK_FIVE),
                C(RANK_SIX),
                C(RANK_SEVEN),
                C(RANK_EIGHT),
                C(RANK_NINE),
                C(RANK_NINE, SUIT_HEARTS)
            }),
            rules::MakeCards({C(RANK_ACE)}),
            rules::MakeCards({C(RANK_KING)})
        },
        PLAYER_HUMAN,
        std::nullopt,
        PLAYER_AI1);

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
            rules::MakeCards({
                C(RANK_THREE),
                C(RANK_THREE, SUIT_HEARTS),
                C(RANK_THREE, SUIT_DIAMONDS),
                C(RANK_NINE)
            }),
            rules::MakeCards({C(RANK_ACE)}),
            rules::MakeCards({C(RANK_KING)})
        },
        PLAYER_HUMAN,
        std::nullopt,
        PLAYER_AI1);

    CHECK(tripleState.SelectBestPatternFromDraggedCards({0, 1, 2}));
    CHECK(tripleState.SelectedIndices().size() == 3);
    CHECK(tripleState.SelectedIndices().contains(0));
    CHECK(tripleState.SelectedIndices().contains(1));
    CHECK(tripleState.SelectedIndices().contains(2));

    game::GameState planeState;
    planeState.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::MakeCards({
                C(RANK_THREE),
                C(RANK_THREE, SUIT_HEARTS),
                C(RANK_THREE, SUIT_DIAMONDS),
                C(RANK_FOUR),
                C(RANK_FOUR, SUIT_HEARTS),
                C(RANK_FOUR, SUIT_DIAMONDS),
                C(RANK_NINE)
            }),
            rules::MakeCards({C(RANK_ACE)}),
            rules::MakeCards({C(RANK_KING)})
        },
        PLAYER_HUMAN,
        std::nullopt,
        PLAYER_AI1);

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
            rules::MakeCards({
                C(RANK_THREE),
                C(RANK_THREE, SUIT_HEARTS),
                C(RANK_THREE, SUIT_DIAMONDS),
                C(RANK_NINE)
            }),
            rules::MakeCards({C(RANK_ACE)}),
            rules::MakeCards({C(RANK_KING)})
        },
        PLAYER_HUMAN,
        std::nullopt,
        PLAYER_AI1);

    REQUIRE(state.SelectBestPatternFromDraggedCards({0, 1, 2}));
    REQUIRE(state.SelectedIndices().size() == 3);

    REQUIRE(state.SelectBestPatternFromDraggedCards({0, 1, 2}));
    CHECK(state.SelectedIndices().empty());
    CHECK(state.HintIndices().empty());
}

TEST_CASE("drag selection completes triple core as triple with two") {
    game::GameState tripleTwoFromSinglesState;
    tripleTwoFromSinglesState.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::MakeCards({
                C(RANK_THREE),
                C(RANK_THREE, SUIT_HEARTS),
                C(RANK_THREE, SUIT_DIAMONDS),
                C(RANK_SEVEN),
                C(RANK_NINE)
            }),
            rules::MakeCards({C(RANK_ACE)}),
            rules::MakeCards({C(RANK_KING)})
        },
        PLAYER_HUMAN,
        std::nullopt,
        PLAYER_AI1);

    REQUIRE(tripleTwoFromSinglesState.SelectBestPatternFromDraggedCards({0, 1, 2}));
    REQUIRE(tripleTwoFromSinglesState.SelectBestPatternFromDraggedCards({3}));
    CHECK(tripleTwoFromSinglesState.SelectedIndices().size() == 4);
    CHECK_FALSE(tripleTwoFromSinglesState.PlaySelected());

    REQUIRE(tripleTwoFromSinglesState.SelectBestPatternFromDraggedCards({4}));
    CHECK(tripleTwoFromSinglesState.SelectedIndices().size() == 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(tripleTwoFromSinglesState.SelectedIndices().contains(i));
    }
    CHECK(tripleTwoFromSinglesState.PlaySelected());

    game::GameState tripleTwoState;
    tripleTwoState.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::MakeCards({
                C(RANK_FOUR),
                C(RANK_FOUR, SUIT_HEARTS),
                C(RANK_FOUR, SUIT_DIAMONDS),
                C(RANK_SEVEN),
                C(RANK_NINE),
                C(RANK_JACK)
            }),
            rules::MakeCards({C(RANK_ACE)}),
            rules::MakeCards({C(RANK_KING)})
        },
        PLAYER_HUMAN,
        std::nullopt,
        PLAYER_AI1);

    REQUIRE(tripleTwoState.SelectBestPatternFromDraggedCards({0, 1, 2}));
    REQUIRE(tripleTwoState.SelectBestPatternFromDraggedCards({3, 4}));
    CHECK(tripleTwoState.SelectedIndices().size() == 5);
    for (int i = 0; i < 5; ++i) {
        CHECK(tripleTwoState.SelectedIndices().contains(i));
    }
    CHECK_FALSE(tripleTwoState.SelectedIndices().contains(5));
    CHECK(tripleTwoState.PlaySelected());
}

TEST_CASE("drag selection adds wings to an existing plane core") {
    game::GameState state;
    state.TestSetRound(
        std::array<rules::Cards, 3>{
            rules::MakeCards({
                C(RANK_THREE),
                C(RANK_THREE, SUIT_HEARTS),
                C(RANK_THREE, SUIT_DIAMONDS),
                C(RANK_FOUR),
                C(RANK_FOUR, SUIT_HEARTS),
                C(RANK_FOUR, SUIT_DIAMONDS),
                C(RANK_SEVEN),
                C(RANK_NINE),
                C(RANK_JACK)
            }),
            rules::MakeCards({C(RANK_ACE)}),
            rules::MakeCards({C(RANK_KING)})
        },
        PLAYER_HUMAN,
        std::nullopt,
        PLAYER_AI1);

    REQUIRE(state.SelectBestPatternFromDraggedCards({0, 1, 2, 3, 4, 5}));
    REQUIRE(state.SelectBestPatternFromDraggedCards({6, 7}));
    CHECK(state.SelectedIndices().size() == 8);
    for (int i = 0; i < 8; ++i) {
        CHECK(state.SelectedIndices().contains(i));
    }
    CHECK_FALSE(state.SelectedIndices().contains(8));
    CHECK(state.PlaySelected());
}
