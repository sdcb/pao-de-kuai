#include "stats/StatStore.h"

#include "core/Str.h"
#include "core/WinFile.h"

#include <cJSON.h>

#include <stdlib.h>
#include <string.h>

#include <windows.h>

/* ---- small helpers --------------------------------------------------- */

static void CopyText(char *dst, int cap, const char *src)
{
    int i = 0;

    if (cap <= 0) {
        return;
    }
    if (src != NULL) {
        while (src[i] != '\0' && i + 1 < cap) {
            dst[i] = src[i];
            ++i;
        }
    }
    dst[i] = '\0';
}

/* Builds "YYYYMMDD" / "HH:MM:SS" with the same digits strftime produced, using
 * Str_AppendPaddedNumber instead of snprintf to stay off the CRT formatter. */
static void FormatLocalDateTime(const SYSTEMTIME *st, bool dateOnly,
                               char *out, int cap)
{
    Str text;

    Str_Init(&text);
    if (dateOnly) {
        Str_AppendPaddedNumber(&text, st->wYear, 4);
        Str_AppendPaddedNumber(&text, st->wMonth, 2);
        Str_AppendPaddedNumber(&text, st->wDay, 2);
    } else {
        Str_AppendPaddedNumber(&text, st->wHour, 2);
        Str_AppendChar(&text, ':');
        Str_AppendPaddedNumber(&text, st->wMinute, 2);
        Str_AppendChar(&text, ':');
        Str_AppendPaddedNumber(&text, st->wSecond, 2);
    }
    CopyText(out, cap, Str_CStr(&text));
    Str_Free(&text);
}

/* cJSON has no snprintf-free integer-key walker, so the score object is spelled
 * out three times exactly as before. */
static cJSON *ScoresToJson(const int scores[3])
{
    cJSON *object = cJSON_CreateObject();
    cJSON_AddNumberToObject(object, "player", scores[0]);
    cJSON_AddNumberToObject(object, "ai1", scores[1]);
    cJSON_AddNumberToObject(object, "ai2", scores[2]);
    return object;
}

static void ScoresFromJson(const cJSON *object, int out[3])
{
    const cJSON *value;

    if (!cJSON_IsObject(object)) {
        return;
    }
    value = cJSON_GetObjectItemCaseSensitive(object, "player");
    if (cJSON_IsNumber(value)) {
        out[0] = value->valueint;
    }
    value = cJSON_GetObjectItemCaseSensitive(object, "ai1");
    if (cJSON_IsNumber(value)) {
        out[1] = value->valueint;
    }
    value = cJSON_GetObjectItemCaseSensitive(object, "ai2");
    if (cJSON_IsNumber(value)) {
        out[2] = value->valueint;
    }
}

static PlayerId PlayerFromKey(const char *key)
{
    if (key == NULL) {
        return PLAYER_HUMAN;
    }
    if (strcmp(key, "ai1") == 0) {
        return PLAYER_AI1;
    }
    if (strcmp(key, "ai2") == 0) {
        return PLAYER_AI2;
    }
    return PLAYER_HUMAN;
}

static cJSON *RoundToJson(const RoundRecord *round)
{
    cJSON *object = cJSON_CreateObject();
    cJSON *bombs;
    cJSON *spring;
    cJSON *losers;

    cJSON_AddStringToObject(object, "startedAt", round->startedAt);
    cJSON_AddStringToObject(object, "endedAt", round->endedAt);
    cJSON_AddStringToObject(object, "winner", PlayerKey(round->winner));
    cJSON_AddStringToObject(object, "playerName", round->playerName);
    cJSON_AddItemToObject(object, "scores", ScoresToJson(round->scores));
    cJSON_AddItemToObject(object, "remainingCards", ScoresToJson(round->remainingCards));

    bombs = cJSON_CreateArray();
    for (int i = 0; i < round->bombCount; ++i) {
        const BombScoreEvent *bomb = &round->bombs[i];
        cJSON *item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "by", PlayerKey(bomb->by));
        cJSON_AddNumberToObject(item, "score", bomb->score);
        cJSON_AddBoolToObject(item, "beaten", bomb->beaten);
        cJSON_AddItemToArray(bombs, item);
    }
    cJSON_AddItemToObject(object, "bombs", bombs);

    spring = cJSON_CreateObject();
    cJSON_AddBoolToObject(spring, "enabled", round->spring.enabled);
    losers = cJSON_CreateArray();
    for (int i = 0; i < round->spring.loserCount; ++i) {
        cJSON_AddItemToArray(losers, cJSON_CreateString(PlayerKey(round->spring.losers[i])));
    }
    cJSON_AddItemToObject(spring, "losers", losers);
    cJSON_AddItemToObject(object, "spring", spring);
    return object;
}

static void RoundFromJson(const cJSON *object, RoundRecord *round)
{
    const cJSON *value;
    const cJSON *bombs;
    const cJSON *spring;

    RoundRecord_Init(round);
    if (!cJSON_IsObject(object)) {
        return;
    }
    value = cJSON_GetObjectItemCaseSensitive(object, "startedAt");
    if (cJSON_IsString(value)) {
        CopyText(round->startedAt, PDK_TIME_TEXT_CAP, value->valuestring);
    }
    value = cJSON_GetObjectItemCaseSensitive(object, "endedAt");
    if (cJSON_IsString(value)) {
        CopyText(round->endedAt, PDK_TIME_TEXT_CAP, value->valuestring);
    }
    value = cJSON_GetObjectItemCaseSensitive(object, "winner");
    if (cJSON_IsString(value)) {
        round->winner = PlayerFromKey(value->valuestring);
    }
    value = cJSON_GetObjectItemCaseSensitive(object, "playerName");
    if (cJSON_IsString(value)) {
        CopyText(round->playerName, PDK_PLAYER_NAME_CAP, value->valuestring);
    }
    ScoresFromJson(cJSON_GetObjectItemCaseSensitive(object, "scores"), round->scores);
    ScoresFromJson(cJSON_GetObjectItemCaseSensitive(object, "remainingCards"),
                   round->remainingCards);

    bombs = cJSON_GetObjectItemCaseSensitive(object, "bombs");
    if (cJSON_IsArray(bombs)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, bombs) {
            BombScoreEvent bomb;
            memset(&bomb, 0, sizeof(bomb));
            value = cJSON_GetObjectItemCaseSensitive(item, "by");
            if (cJSON_IsString(value)) {
                bomb.by = PlayerFromKey(value->valuestring);
            }
            value = cJSON_GetObjectItemCaseSensitive(item, "score");
            if (cJSON_IsNumber(value)) {
                bomb.score = value->valueint;
            }
            value = cJSON_GetObjectItemCaseSensitive(item, "beaten");
            if (cJSON_IsBool(value)) {
                bomb.beaten = cJSON_IsTrue(value) != 0;
            }
            RoundRecord_AddBomb(round, bomb);
        }
    }

    spring = cJSON_GetObjectItemCaseSensitive(object, "spring");
    if (cJSON_IsObject(spring)) {
        value = cJSON_GetObjectItemCaseSensitive(spring, "enabled");
        if (cJSON_IsBool(value)) {
            round->spring.enabled = cJSON_IsTrue(value) != 0;
        }
        value = cJSON_GetObjectItemCaseSensitive(spring, "losers");
        if (cJSON_IsArray(value)) {
            const cJSON *item = NULL;
            cJSON_ArrayForEach(item, value) {
                if (cJSON_IsString(item) && round->spring.loserCount < 3) {
                    round->spring.losers[round->spring.loserCount++] =
                        PlayerFromKey(item->valuestring);
                }
            }
        }
    }
}

static void Accumulate(StatSummary *summary, const DailyStat *day)
{
    for (int i = 0; i < day->roundCount; ++i) {
        const RoundRecord *round = &day->rounds[i];
        summary->rounds++;
        for (int seat = 0; seat < 3; ++seat) {
            summary->scores[seat] += round->scores[seat];
        }
        summary->bombs += round->bombCount;
        summary->springLosers += round->spring.loserCount;
        if (round->scores[0] > summary->bestSingleRoundPlayerScore) {
            summary->bestSingleRoundPlayerScore = round->scores[0];
        }
    }
}

/* ---- RoundRecord / DailyStat / StatSummary --------------------------- */

void RoundRecord_Init(RoundRecord *record)
{
    memset(record, 0, sizeof(*record));
    record->winner = PLAYER_HUMAN;
}

bool RoundRecord_AddBomb(RoundRecord *record, BombScoreEvent bomb)
{
    if (record->bombCount >= BOMB_EVENTS_MAX) {
        return false;
    }
    record->bombs[record->bombCount++] = bomb;
    return true;
}

void DailyStat_Init(DailyStat *day)
{
    memset(day, 0, sizeof(*day));
}

void DailyStat_Free(DailyStat *day)
{
    free(day->rounds);
    DailyStat_Init(day);
}

bool DailyStat_Append(DailyStat *day, const RoundRecord *round)
{
    if (day->roundCount == day->roundCapacity) {
        const int next = day->roundCapacity > 0 ? day->roundCapacity * 2 : 8;
        RoundRecord *grown =
            (RoundRecord *)realloc(day->rounds, (size_t)next * sizeof(RoundRecord));
        if (grown == NULL) {
            return false;
        }
        day->rounds = grown;
        day->roundCapacity = next;
    }
    day->rounds[day->roundCount++] = *round;
    return true;
}

void StatSummary_Init(StatSummary *summary)
{
    memset(summary, 0, sizeof(*summary));
}

/* ---- StatStore ------------------------------------------------------- */

void StatStore_Init(StatStore *store, const char *root)
{
    memset(store, 0, sizeof(*store));
    if (root != NULL && root[0] != '\0') {
        CopyText(store->root, PDK_STAT_ROOT_CAP, root);
        return;
    }
    {
        Str current;
        Str_Init(&current);
        WinFile_CurrentDirectory(&current);
        CopyText(store->root, PDK_STAT_ROOT_CAP, Str_CStr(&current));
        Str_Free(&current);
    }
}

void StatStore_DayPath(const StatStore *store, const char *date, char *out, int cap)
{
    Str statDir;
    Str fileName;
    Str path;

    Str_Init(&statDir);
    Str_Init(&fileName);
    Str_Init(&path);

    WinFile_JoinPath(&statDir, store->root, "stat");
    Str_Append(&fileName, date);
    Str_Append(&fileName, ".json");
    WinFile_JoinPath(&path, Str_CStr(&statDir), Str_CStr(&fileName));
    CopyText(out, cap, Str_CStr(&path));

    Str_Free(&statDir);
    Str_Free(&fileName);
    Str_Free(&path);
}

bool StatStore_LoadDay(const StatStore *store, const char *date, DailyStat *out)
{
    char path[PDK_STAT_PATH_CAP];
    Str content;
    cJSON *root;
    const cJSON *value;
    const cJSON *rounds;
    bool ok = false;

    DailyStat_Init(out);
    CopyText(out->date, PDK_DATE_KEY_CAP, date);

    StatStore_DayPath(store, date, path, (int)sizeof(path));
    Str_Init(&content);
    if (!WinFile_ReadTextFile(path, &content)) {
        Str_Free(&content);
        return false;
    }

    root = cJSON_Parse(Str_CStr(&content));
    Str_Free(&content);
    if (root == NULL) {
        return false;
    }

    value = cJSON_GetObjectItemCaseSensitive(root, "date");
    if (cJSON_IsString(value)) {
        CopyText(out->date, PDK_DATE_KEY_CAP, value->valuestring);
    }
    rounds = cJSON_GetObjectItemCaseSensitive(root, "rounds");
    if (cJSON_IsArray(rounds)) {
        const cJSON *item = NULL;
        cJSON_ArrayForEach(item, rounds) {
            RoundRecord round;
            RoundFromJson(item, &round);
            DailyStat_Append(out, &round);
        }
    }
    ok = true;
    cJSON_Delete(root);
    return ok;
}

bool StatStore_SaveDay(const StatStore *store, const DailyStat *day)
{
    char statDir[PDK_STAT_PATH_CAP];
    char path[PDK_STAT_PATH_CAP];
    Str joined;
    cJSON *root;
    cJSON *rounds;
    char *text;
    bool ok;

    Str_Init(&joined);
    WinFile_JoinPath(&joined, store->root, "stat");
    CopyText(statDir, PDK_STAT_PATH_CAP, Str_CStr(&joined));
    Str_Free(&joined);
    WinFile_CreateDirectories(statDir);

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "date", day->date);
    rounds = cJSON_CreateArray();
    for (int i = 0; i < day->roundCount; ++i) {
        cJSON_AddItemToArray(rounds, RoundToJson(&day->rounds[i]));
    }
    cJSON_AddItemToObject(root, "rounds", rounds);

    text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return false;
    }

    StatStore_DayPath(store, day->date, path, (int)sizeof(path));
    ok = WinFile_WriteTextFile(path, text);
    cJSON_free(text);
    return ok;
}

bool StatStore_AppendRound(const StatStore *store, const char *date,
                           const RoundRecord *round)
{
    DailyStat day;
    bool ok;

    StatStore_LoadDay(store, date, &day);
    CopyText(day.date, PDK_DATE_KEY_CAP, date);
    if (!DailyStat_Append(&day, round)) {
        DailyStat_Free(&day);
        return false;
    }
    ok = StatStore_SaveDay(store, &day);
    DailyStat_Free(&day);
    return ok;
}

void StatStore_SummarizeDay(const StatStore *store, const char *date, StatSummary *out)
{
    DailyStat day;

    StatSummary_Init(out);
    StatStore_LoadDay(store, date, &day);
    Accumulate(out, &day);
    DailyStat_Free(&day);
}

/* `prefix` is a date key ("20260606") or a month key ("202606"); a round is
 * included when the file stem starts with it ("" matches everything). */
static bool StartsWith(const char *text, const char *prefix)
{
    if (prefix == NULL) {
        return true;
    }
    while (*prefix != '\0') {
        if (*text != *prefix) {
            return false;
        }
        ++text;
        ++prefix;
    }
    return true;
}

static void SummarizeMatching(const StatStore *store, const char *prefix, StatSummary *out)
{
    Str statDir;
    StrList files;

    StatSummary_Init(out);
    Str_Init(&statDir);
    StrList_Init(&files);

    WinFile_JoinPath(&statDir, store->root, "stat");
    if (WinFile_DirectoryExists(Str_CStr(&statDir)) &&
        WinFile_ListRegularFileNames(Str_CStr(&statDir), &files)) {
        for (int i = 0; i < files.count; ++i) {
            const char *fileName = StrList_At(&files, i);
            Str stem;
            Str extension;
            DailyStat day;

            Str_Init(&stem);
            Str_Init(&extension);
            WinFile_FileStem(&stem, fileName);
            WinFile_FileExtension(&extension, fileName);
            if (Str_Equals(&extension, ".json") && StartsWith(Str_CStr(&stem), prefix)) {
                StatStore_LoadDay(store, Str_CStr(&stem), &day);
                Accumulate(out, &day);
                DailyStat_Free(&day);
            }
            Str_Free(&stem);
            Str_Free(&extension);
        }
    }
    StrList_Free(&files);
    Str_Free(&statDir);
}

void StatStore_SummarizeMonth(const StatStore *store, const char *yyyymm, StatSummary *out)
{
    SummarizeMatching(store, yyyymm, out);
}

void StatStore_SummarizeHistory(const StatStore *store, StatSummary *out)
{
    SummarizeMatching(store, "", out);
}

void TodayDateKey(char *out, int cap)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    FormatLocalDateTime(&st, true, out, cap);
}

void NowTimeText(char *out, int cap)
{
    SYSTEMTIME st;
    GetLocalTime(&st);
    FormatLocalDateTime(&st, false, out, cap);
}
