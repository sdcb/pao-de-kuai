#include <doctest/doctest.h>

#include "rules/Scoring.h"

using namespace pdk;

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
