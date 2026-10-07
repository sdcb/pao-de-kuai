#include <doctest/doctest.h>

#include "rules/CppCompat.h"

using namespace pdk;

/*
 * RoundScoreInput is a plain C aggregate now, so its arrays have to be filled in
 * field by field (C++ cannot assign an initializer list to an array member).
 * These helpers keep the cases readable; the expectations are unchanged.
 */
namespace {

void SetRemaining(rules::RoundScoreInput& input, int human, int ai1, int ai2)
{
    input.remainingCards[0] = human;
    input.remainingCards[1] = ai1;
    input.remainingCards[2] = ai2;
}

void SetPlayed(rules::RoundScoreInput& input, bool human, bool ai1, bool ai2)
{
    input.hasPlayedCards[0] = human;
    input.hasPlayedCards[1] = ai1;
    input.hasPlayedCards[2] = ai2;
}

void AddBomb(rules::RoundScoreInput& input, rules::PlayerId by, int score, bool beaten)
{
    REQUIRE(input.bombCount < BOMB_EVENTS_MAX);
    input.bombs[input.bombCount].by = by;
    input.bombs[input.bombCount].score = score;
    input.bombs[input.bombCount].beaten = beaten;
    ++input.bombCount;
}

void CheckScores(const rules::RoundScoreResult& result, int human, int ai1, int ai2)
{
    CHECK(result.scores[0] == human);
    CHECK(result.scores[1] == ai1);
    CHECK(result.scores[2] == ai2);
}

} // namespace

TEST_CASE("scoring examples and bomb fixed points") {
    rules::RoundScoreInput input{};
    input.winner = PLAYER_HUMAN;
    SetRemaining(input, 0, 8, 1);
    SetPlayed(input, true, true, true);
    rules::RoundScoreResult score = rules::CalculateRoundScore(input);
    CheckScores(score, 8, -8, 0);

    SetRemaining(input, 0, 16, 5);
    SetPlayed(input, true, false, true);
    score = rules::CalculateRoundScore(input);
    CheckScores(score, 37, -32, -5);

    SetRemaining(input, 0, 16, 16);
    SetPlayed(input, true, false, false);
    score = rules::CalculateRoundScore(input);
    CheckScores(score, 64, -32, -32);
    CHECK(score.spring.enabled);

    SetRemaining(input, 0, 8, 10);
    SetPlayed(input, true, true, true);
    AddBomb(input, PLAYER_HUMAN, 20, false);
    score = rules::CalculateRoundScore(input);
    CheckScores(score, 38, -18, -20);
}

TEST_CASE("a bomb beaten by a bigger bomb scores nothing") {
    rules::RoundScoreInput input{};
    input.winner = PLAYER_HUMAN;
    SetRemaining(input, 0, 0, 0);
    SetPlayed(input, true, true, true);
    AddBomb(input, PLAYER_AI2, 20, true);
    AddBomb(input, PLAYER_HUMAN, 20, false);
    const rules::RoundScoreResult score = rules::CalculateRoundScore(input);
    CheckScores(score, 20, -10, -10);
    CHECK(score.scores[0] + score.scores[1] + score.scores[2] == 0);
}
