#pragma once

/*
 * Per-turn audit record: what the actor saw, what it asked for, what was accepted.
 *
 * Pure C.  This is the widest struct in the game layer, so the members are all
 * fixed-size:
 *   - text fields are char buffers (Str_CopyTo fills them);
 *   - `optional<HandPattern>` became a value plus a `has` flag;
 *   - `GameAction.ranks` was a std::vector<std::string> and is now a fixed array of
 *     short strings, which is enough because an action never names more ranks than a
 *     hand holds.
 * The whole record is therefore copyable and holds no pointers into the heap.
 */

#include "game/StrategyMetadata.h"
#include "rules/Card.h"
#include "rules/HandPattern.h"
#include "rules/Scoring.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PDK_TURN_TEXT_CAP = 256 };
enum { PDK_ACTION_TEXT_CAP = 64 };
enum { PDK_ACTION_RANK_CAP = 16 };
enum { PDK_ACTION_RANKS_MAX = 20 };

typedef uint8_t TurnDecisionSource;
typedef uint8_t TurnDecisionReason;

enum {
    TURN_SOURCE_HUMAN = 0,
    TURN_SOURCE_LOCAL_AI = 1,
    TURN_SOURCE_SYSTEM = 2
};

enum {
    TURN_REASON_NORMAL_CHOICE = 0,
    TURN_REASON_CANNOT_BEAT = 1,
    TURN_REASON_ONLY_LEGAL_MOVE = 2
};

typedef struct GameAction {
    char action[PDK_ACTION_TEXT_CAP];
    char ranks[PDK_ACTION_RANKS_MAX][PDK_ACTION_RANK_CAP];
    int rankCount;
} GameAction;

/* `HandPattern` is always meaningful when `hasPattern` is true; the flag replaces the
 * std::optional the C++ version used. */
typedef struct TurnSnapshot {
    Cards hands[3];
    Cards lastCards;
    HandPattern lastPattern;
    bool hasLastPattern;
    PlayerId lastMovePlayer;
    PlayerId currentPlayer;
    int passCount;
} TurnSnapshot;

typedef struct TurnDecisionTrace {
    char reasoningContent[PDK_TURN_TEXT_CAP];
    char errorMessage[PDK_TURN_TEXT_CAP];
} TurnDecisionTrace;

typedef struct TurnRecord {
    int turnNo;
    PlayerId actor;
    TurnDecisionSource source;
    TurnDecisionReason reason;
    TurnSnapshot before;
    TurnSnapshot after;
    GameAction requestedAction;
    GameAction finalAction;
    Cards finalCards;
    HandPattern finalPattern;
    bool hasFinalPattern;
    bool accepted;
    char validationMessage[PDK_TURN_TEXT_CAP];
    StrategyMetadata strategy;
    TurnDecisionTrace trace;
} TurnRecord;

void GameAction_Clear(GameAction *action);
void GameAction_Set(GameAction *action, const char *name);
bool GameAction_AddRank(GameAction *action, const char *rank);

/* All four return a string literal, so nothing to free. */
const char *PlayerLabel(PlayerId player);
const char *SourceLabel(TurnDecisionSource source);
const char *ReasonLabel(TurnDecisionReason reason);

#ifdef __cplusplus
}
#endif
