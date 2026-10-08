#include "rules/Scoring.h"

#include <string.h>

int PlayerIndex(PlayerId player)
{
    return (int)player;
}

PlayerId PlayerFromIndex(int index)
{
    switch (index) {
    case 0: return PLAYER_HUMAN;
    case 1: return PLAYER_AI1;
    default: return PLAYER_AI2;
    }
}

const char *PlayerKey(PlayerId player)
{
    switch (player) {
    case PLAYER_HUMAN: return "player";
    case PLAYER_AI1: return "ai1";
    case PLAYER_AI2: return "ai2";
    }
    return "unknown";
}

RoundScoreResult CalculateRoundScore(const RoundScoreInput *input)
{
    RoundScoreResult result;
    int winner;

    memset(&result, 0, sizeof(result));

    for (int i = 0; i < input->bombCount; ++i) {
        const BombScoreEvent *bomb = &input->bombs[i];
        int bombPlayer;
        if (bomb->beaten) {
            continue;
        }
        bombPlayer = PlayerIndex(bomb->by);
        result.scores[bombPlayer] += bomb->score;
        for (int j = 0; j < 3; ++j) {
            if (j != bombPlayer) {
                result.scores[j] -= bomb->score / 2;
            }
        }
    }

    winner = PlayerIndex(input->winner);
    for (int i = 0; i < 3; ++i) {
        int remaining;
        if (i == winner) {
            continue;
        }

        if (!input->hasPlayedCards[i]) {
            result.spring.enabled = true;
            if (result.spring.loserCount < 3) {
                result.spring.losers[result.spring.loserCount++] = PlayerFromIndex(i);
            }
            result.scores[i] -= 32;
            result.scores[winner] += 32;
            continue;
        }

        remaining = input->remainingCards[i] == 1 ? 0 : input->remainingCards[i];
        result.scores[i] -= remaining;
        result.scores[winner] += remaining;
    }

    return result;
}
