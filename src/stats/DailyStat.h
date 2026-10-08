#pragma once

/*
 * One recorded round plus a day's worth of them, and the aggregate summary.
 *
 * Pure C.  RoundRecord is a plain value type with fixed buffers: a round has one
 * timestamp pair, one name, three scores and at most eleven bombs, so nothing here
 * needs the heap and the whole record is freely copyable.
 *
 * DailyStat is the one place a day's rounds can be arbitrarily many, so it owns a
 * growable array behind Init/Free/Append (there is no RAII in C).
 */

#include "rules/Card.h"
#include "rules/Scoring.h"
#include "stats/AppSettings.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PDK_TIME_TEXT_CAP = 24 };
enum { PDK_DATE_KEY_CAP = 16 };

typedef struct RoundRecord {
    char startedAt[PDK_TIME_TEXT_CAP];
    char endedAt[PDK_TIME_TEXT_CAP];
    PlayerId winner;
    char playerName[PDK_PLAYER_NAME_CAP];
    int scores[3];
    int remainingCards[3];
    BombScoreEvent bombs[BOMB_EVENTS_MAX];
    int bombCount;
    SpringInfo spring;
} RoundRecord;

/* Zeroes everything and sets winner to PLAYER_HUMAN, matching the old defaulted
 * member initialisers. */
void RoundRecord_Init(RoundRecord *record);
bool RoundRecord_AddBomb(RoundRecord *record, BombScoreEvent bomb);

typedef struct DailyStat {
    char date[PDK_DATE_KEY_CAP];
    RoundRecord *rounds;
    int roundCount;
    int roundCapacity;
} DailyStat;

void DailyStat_Init(DailyStat *day);
void DailyStat_Free(DailyStat *day);
bool DailyStat_Append(DailyStat *day, const RoundRecord *round);

typedef struct StatSummary {
    int rounds;
    int scores[3];
    int bombs;
    int springLosers;
    int bestSingleRoundPlayerScore;
} StatSummary;

void StatSummary_Init(StatSummary *summary);

#ifdef __cplusplus
}
#endif
