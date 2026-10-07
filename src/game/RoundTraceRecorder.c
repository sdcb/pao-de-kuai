#include "game/RoundTraceRecorder.h"

#include "core/Str.h"
#include "core/WinFile.h"

#include <cJSON.h>

#ifndef PDK_APP_VERSION
#define PDK_APP_VERSION "unknown"
#endif

#ifndef PDK_BUILD_REVISION
#define PDK_BUILD_REVISION "unknown"
#endif

static cJSON *CardsToJson(const Cards *cards)
{
    cJSON *array = cJSON_CreateArray();

    for (int i = 0; i < cards->count; ++i) {
        char text[8];
        Card_ToString(cards->items[i], text, (int)sizeof(text));
        cJSON_AddItemToArray(array, cJSON_CreateString(text));
    }
    return array;
}

/* A NULL pattern means "there was none", which is what the old nullopt produced. */
static cJSON *PatternToJson(const HandPattern *pattern)
{
    cJSON *object;
    char description[PATTERN_REASON_CAP];

    if (pattern == NULL) {
        return cJSON_CreateNull();
    }
    object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "type", PatternName(pattern->type));
    cJSON_AddStringToObject(object, "mainRank", RankName(pattern->mainRank));
    cJSON_AddNumberToObject(object, "cardCount", pattern->cardCount);
    cJSON_AddNumberToObject(object, "groupCount", pattern->groupCount);
    cJSON_AddBoolToObject(object, "lastHandShort", pattern->lastHandShort);
    PatternDescription(pattern, description, (int)sizeof(description));
    cJSON_AddStringToObject(object, "description", description);
    return object;
}

static cJSON *ActionToJson(const GameAction *action)
{
    cJSON *object = cJSON_CreateObject();
    cJSON *ranks = cJSON_CreateArray();

    cJSON_AddStringToObject(object, "action", action->action);
    for (int i = 0; i < action->rankCount; ++i) {
        cJSON_AddItemToArray(ranks, cJSON_CreateString(action->ranks[i]));
    }
    cJSON_AddItemToObject(object, "ranks", ranks);
    return object;
}

static cJSON *HandsToJson(const Cards *hands)
{
    cJSON *object = cJSON_CreateObject();

    cJSON_AddItemToObject(object, "player", CardsToJson(&hands[0]));
    cJSON_AddItemToObject(object, "ai1", CardsToJson(&hands[1]));
    cJSON_AddItemToObject(object, "ai2", CardsToJson(&hands[2]));
    return object;
}

static cJSON *SnapshotToJson(const TurnSnapshot *snapshot)
{
    cJSON *object = cJSON_CreateObject();
    cJSON *remaining = cJSON_CreateObject();

    cJSON_AddItemToObject(object, "hands", HandsToJson(snapshot->hands));
    cJSON_AddItemToObject(object, "lastCards", CardsToJson(&snapshot->lastCards));
    cJSON_AddItemToObject(object, "lastPattern",
                          PatternToJson(snapshot->hasLastPattern ? &snapshot->lastPattern : NULL));
    cJSON_AddStringToObject(object, "lastMovePlayer", PlayerKey(snapshot->lastMovePlayer));
    cJSON_AddStringToObject(object, "currentPlayer", PlayerKey(snapshot->currentPlayer));
    cJSON_AddNumberToObject(object, "passCount", snapshot->passCount);
    cJSON_AddNumberToObject(remaining, "player", snapshot->hands[0].count);
    cJSON_AddNumberToObject(remaining, "ai1", snapshot->hands[1].count);
    cJSON_AddNumberToObject(remaining, "ai2", snapshot->hands[2].count);
    cJSON_AddItemToObject(object, "remainingCards", remaining);
    return object;
}

static cJSON *TraceMetaToJson(const TurnDecisionTrace *trace)
{
    cJSON *object = cJSON_CreateObject();

    if (trace->reasoningContent[0] != '\0') {
        cJSON_AddStringToObject(object, "reasoningContent", trace->reasoningContent);
    }
    if (trace->errorMessage[0] != '\0') {
        cJSON_AddStringToObject(object, "errorMessage", trace->errorMessage);
    }
    return object;
}

static void AddStrategyFields(cJSON *object, const StrategyMetadata *strategy)
{
    cJSON_AddStringToObject(object, "strategy", strategy->strategy);
    cJSON_AddStringToObject(object, "strategyVersion", strategy->strategyVersion);
}

static cJSON *TurnToJson(const TurnRecord *record)
{
    cJSON *object = cJSON_CreateObject();

    cJSON_AddNumberToObject(object, "turnNo", record->turnNo);
    cJSON_AddStringToObject(object, "actor", PlayerKey(record->actor));
    cJSON_AddStringToObject(object, "source", SourceLabel(record->source));
    cJSON_AddStringToObject(object, "reason", ReasonLabel(record->reason));
    cJSON_AddBoolToObject(object, "accepted", record->accepted);
    cJSON_AddStringToObject(object, "validationMessage", record->validationMessage);
    AddStrategyFields(object, &record->strategy);
    cJSON_AddItemToObject(object, "before", SnapshotToJson(&record->before));
    cJSON_AddItemToObject(object, "after", SnapshotToJson(&record->after));
    cJSON_AddItemToObject(object, "requestedAction", ActionToJson(&record->requestedAction));
    cJSON_AddItemToObject(object, "finalAction", ActionToJson(&record->finalAction));
    cJSON_AddItemToObject(object, "finalCards", CardsToJson(&record->finalCards));
    cJSON_AddItemToObject(object, "finalPattern",
                          PatternToJson(record->hasFinalPattern ? &record->finalPattern : NULL));
    cJSON_AddItemToObject(object, "trace", TraceMetaToJson(&record->trace));
    return object;
}

static cJSON *PlayersToJson(const PlayerState *players, const StrategyMetadata *strategies)
{
    cJSON *array = cJSON_CreateArray();

    for (int i = 0; i < PDK_AI_SEATS; ++i) {
        cJSON *object = cJSON_CreateObject();
        const PlayerId id = PlayerFromIndex(i);

        cJSON_AddStringToObject(object, "id", PlayerKey(id));
        cJSON_AddStringToObject(object, "name", players[i].name);
        cJSON_AddStringToObject(object, "kind", id == PLAYER_HUMAN ? "human" : "ai");
        AddStrategyFields(object, &strategies[i]);
        cJSON_AddItemToObject(object, "initialHand", CardsToJson(&players[i].hand));
        cJSON_AddItemToArray(array, object);
    }
    return array;
}

static cJSON *ScoresToJson(const int *values)
{
    cJSON *object = cJSON_CreateObject();

    cJSON_AddNumberToObject(object, "player", values[0]);
    cJSON_AddNumberToObject(object, "ai1", values[1]);
    cJSON_AddNumberToObject(object, "ai2", values[2]);
    return object;
}

static cJSON *ResultToJson(const RoundRecord *result)
{
    cJSON *object = cJSON_CreateObject();
    cJSON *bombs = cJSON_CreateArray();
    cJSON *spring = cJSON_CreateObject();
    cJSON *losers = cJSON_CreateArray();

    cJSON_AddStringToObject(object, "startedAt", result->startedAt);
    cJSON_AddStringToObject(object, "endedAt", result->endedAt);
    cJSON_AddStringToObject(object, "winner", PlayerKey(result->winner));
    cJSON_AddItemToObject(object, "scores", ScoresToJson(result->scores));
    cJSON_AddItemToObject(object, "remainingCards", ScoresToJson(result->remainingCards));

    for (int i = 0; i < result->bombCount; ++i) {
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "by", PlayerKey(result->bombs[i].by));
        cJSON_AddNumberToObject(item, "score", result->bombs[i].score);
        cJSON_AddBoolToObject(item, "beaten", result->bombs[i].beaten);
        cJSON_AddItemToArray(bombs, item);
    }
    cJSON_AddItemToObject(object, "bombs", bombs);

    cJSON_AddBoolToObject(spring, "enabled", result->spring.enabled);
    for (int i = 0; i < result->spring.loserCount; ++i) {
        cJSON_AddItemToArray(losers, cJSON_CreateString(PlayerKey(result->spring.losers[i])));
    }
    cJSON_AddItemToObject(spring, "losers", losers);
    cJSON_AddItemToObject(object, "spring", spring);
    return object;
}

/* "12:34:56" -> "123456"; the old code did the same erase-and-fallback. */
static void TimeKey(const char *value, char *out, int cap)
{
    int written = 0;

    for (int i = 0; value[i] != '\0' && written + 1 < cap; ++i) {
        if (value[i] != ':') {
            out[written++] = value[i];
        }
    }
    out[written] = '\0';
    if (written == 0) {
        Str_CopyTo(out, cap, "000000");
    }
}

static void TracePath(const char *root, const RoundTrace *trace, Str *out)
{
    char date[PDK_DATE_KEY_CAP];
    char timeKey[PDK_TIME_TEXT_CAP];
    Str rootText;
    Str name;
    Str base;
    Str joined;

    Str_Init(&rootText);
    Str_Init(&name);
    Str_Init(&base);
    Str_Init(&joined);

    if (root != NULL && root[0] != '\0') {
        Str_Append(&rootText, root);
    } else {
        WinFile_CurrentDirectory(&rootText);
    }

    TodayDateKey(date, PDK_DATE_KEY_CAP);
    TimeKey(trace->result->endedAt, timeKey, PDK_TIME_TEXT_CAP);

    Str_Append(&name, timeKey);
    Str_Append(&name, "-seed");
    Str_AppendNumber(&name, trace->seed);
    Str_Append(&name, "-turns");
    Str_AppendNumber(&name, trace->turnCount);
    Str_Append(&name, ".json");

    WinFile_JoinPath(&base, Str_CStr(&rootText), "round-traces");
    WinFile_JoinPath(&joined, Str_CStr(&base), date);
    WinFile_JoinPath(out, Str_CStr(&joined), Str_CStr(&name));

    Str_Free(&joined);
    Str_Free(&base);
    Str_Free(&name);
    Str_Free(&rootText);
}

bool RoundTraceRecorder_WriteRound(const char *root, const RoundTrace *trace, char *outPath,
                                   int cap)
{
    cJSON *json = cJSON_CreateObject();
    cJSON *turns;
    char date[PDK_DATE_KEY_CAP];
    char *text;
    Str path;
    bool ok;

    TodayDateKey(date, PDK_DATE_KEY_CAP);
    cJSON_AddNumberToObject(json, "schemaVersion", 2);
    cJSON_AddStringToObject(json, "recordType", "pdk_round_trace");
    cJSON_AddStringToObject(json, "appVersion", PDK_APP_VERSION);
    cJSON_AddStringToObject(json, "buildRevision", PDK_BUILD_REVISION);
    cJSON_AddStringToObject(json, "rulesVersion", "pdk48-v1");
    cJSON_AddStringToObject(json, "turnOrder", "counterclockwise");
    cJSON_AddStringToObject(json, "date", date);
    cJSON_AddNumberToObject(json, "seed", trace->seed);
    cJSON_AddStringToObject(json, "playerName", trace->playerName);
    cJSON_AddStringToObject(json, "startedAt", trace->startedAt);
    cJSON_AddStringToObject(json, "roundLeader", PlayerKey(trace->roundLeader));
    cJSON_AddItemToObject(json, "players",
                          PlayersToJson(trace->initialPlayers, trace->strategies));
    {
        Cards initialHands[PDK_AI_SEATS];
        for (int i = 0; i < PDK_AI_SEATS; ++i) {
            initialHands[i] = trace->initialPlayers[i].hand;
        }
        cJSON_AddItemToObject(json, "initialHands", HandsToJson(initialHands));
    }

    turns = cJSON_CreateArray();
    for (int i = 0; i < trace->turnCount; ++i) {
        cJSON_AddItemToArray(turns, TurnToJson(&trace->turns[i]));
    }
    cJSON_AddItemToObject(json, "turns", turns);
    cJSON_AddItemToObject(json, "result", ResultToJson(trace->result));

    text = cJSON_Print(json);
    cJSON_Delete(json);
    if (text == NULL) {
        return false;
    }

    Str_Init(&path);
    TracePath(root, trace, &path);
    ok = WinFile_WriteTextFile(Str_CStr(&path), text);
    if (ok && outPath != NULL) {
        Str_CopyTo(outPath, cap, Str_CStr(&path));
    }
    Str_Free(&path);
    cJSON_free(text);
    return ok;
}
