#include "game/TurnRecord.h"

#include "core/Str.h"

void GameAction_Clear(GameAction *action)
{
    action->action[0] = '\0';
    action->rankCount = 0;
}

void GameAction_Set(GameAction *action, const char *name)
{
    Str_CopyTo(action->action, PDK_ACTION_TEXT_CAP, name);
}

bool GameAction_AddRank(GameAction *action, const char *rank)
{
    if (action->rankCount >= PDK_ACTION_RANKS_MAX) {
        return false;
    }
    Str_CopyTo(action->ranks[action->rankCount], PDK_ACTION_RANK_CAP, rank);
    ++action->rankCount;
    return true;
}

const char *PlayerLabel(PlayerId player)
{
    switch (player) {
    case PLAYER_HUMAN: return "player";
    case PLAYER_AI1: return "ai1";
    case PLAYER_AI2: return "ai2";
    }
    return "unknown";
}

const char *SourceLabel(TurnDecisionSource source)
{
    switch (source) {
    case TURN_SOURCE_HUMAN: return "human";
    case TURN_SOURCE_LOCAL_AI: return "local_ai";
    case TURN_SOURCE_SYSTEM: return "system";
    }
    return "unknown";
}

const char *ReasonLabel(TurnDecisionReason reason)
{
    switch (reason) {
    case TURN_REASON_NORMAL_CHOICE: return "normal_choice";
    case TURN_REASON_CANNOT_BEAT: return "cannot_beat";
    case TURN_REASON_ONLY_LEGAL_MOVE: return "only_legal_move";
    }
    return "unknown";
}
