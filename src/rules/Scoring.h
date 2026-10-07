#pragma once

#include "rules/Card.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PLAYER_HUMAN = 0,
    PLAYER_AI1 = 1,
    PLAYER_AI2 = 2
};

/* A 48-card deck holds at most eleven four-of-a-kind groups (3..K), so a fixed
 * array is enough and keeps scoring allocation free. */
enum { BOMB_EVENTS_MAX = 12 };

typedef struct BombScoreEvent {
    PlayerId by;
    int score;
    /* True when a bigger bomb was played on top of this one before the trick
     * ended: a beaten bomb scores nothing. */
    bool beaten;
} BombScoreEvent;

typedef struct SpringInfo {
    bool enabled;
    PlayerId losers[3];
    int loserCount;
} SpringInfo;

typedef struct RoundScoreInput {
    PlayerId winner;
    int remainingCards[3];
    bool hasPlayedCards[3];
    BombScoreEvent bombs[BOMB_EVENTS_MAX];
    int bombCount;
} RoundScoreInput;

typedef struct RoundScoreResult {
    int scores[3];
    SpringInfo spring;
} RoundScoreResult;

int PlayerIndex(PlayerId player);
PlayerId PlayerFromIndex(int index);
/* Returns a string literal ("player" / "ai1" / "ai2"), so nothing to free. */
const char *PlayerKey(PlayerId player);
RoundScoreResult CalculateRoundScore(const RoundScoreInput *input);

#ifdef __cplusplus
}
#endif
