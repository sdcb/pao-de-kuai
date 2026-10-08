#include "stats/AppSettings.h"

#include "core/Str.h"
#include "core/WinFile.h"

#include <cJSON.h>

#include <string.h>

/* UTF-8 for "李姐", the default player name in the previous C++ default member
 * initialiser. */
static const char kDefaultPlayerName[] = "\xE6\x9D\x8E\xE5\xA7\x90";

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

static float ClampFloat(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    if (value > high) {
        return high;
    }
    return value;
}

void AppSettings_Default(AppSettings *settings)
{
    memset(settings, 0, sizeof(*settings));
    CopyText(settings->playerName, PDK_PLAYER_NAME_CAP, kDefaultPlayerName);
    settings->masterVolume = 0.8f;
    settings->windowWidth = 1280;
    settings->windowHeight = 720;
    CopyText(settings->ai1, PDK_AI_NAME_CAP, "basic");
    CopyText(settings->ai2, PDK_AI_NAME_CAP, "basic");
    settings->roundTraceEnabled = false;
}

void AppSettings_SetPlayerName(AppSettings *settings, const char *name)
{
    CopyText(settings->playerName, PDK_PLAYER_NAME_CAP, name);
}

void NormalizeAiSelection(const char *value, char *out, int cap)
{
    const bool strong = value != NULL && strcmp(value, "strong") == 0;
    CopyText(out, cap, strong ? "strong" : "basic");
}

void LoadAppSettings(const char *path, AppSettings *out)
{
    Str content;
    cJSON *root;
    const cJSON *value;

    AppSettings_Default(out);

    Str_Init(&content);
    if (!WinFile_ReadTextFile(path != NULL ? path : "appsettings.json", &content)) {
        Str_Free(&content);
        return;
    }

    root = cJSON_Parse(Str_CStr(&content));
    Str_Free(&content);
    if (root == NULL) {
        return;
    }

    value = cJSON_GetObjectItemCaseSensitive(root, "playerName");
    if (cJSON_IsString(value) && value->valuestring != NULL) {
        CopyText(out->playerName, PDK_PLAYER_NAME_CAP, value->valuestring);
    }
    value = cJSON_GetObjectItemCaseSensitive(root, "masterVolume");
    if (cJSON_IsNumber(value)) {
        out->masterVolume = ClampFloat((float)value->valuedouble, 0.0f, 1.0f);
    }
    value = cJSON_GetObjectItemCaseSensitive(root, "windowWidth");
    if (cJSON_IsNumber(value)) {
        out->windowWidth = value->valueint > 1280 ? value->valueint : 1280;
    }
    value = cJSON_GetObjectItemCaseSensitive(root, "windowHeight");
    if (cJSON_IsNumber(value)) {
        out->windowHeight = value->valueint > 720 ? value->valueint : 720;
    }
    value = cJSON_GetObjectItemCaseSensitive(root, "ai1");
    if (cJSON_IsString(value) && value->valuestring != NULL) {
        NormalizeAiSelection(value->valuestring, out->ai1, PDK_AI_NAME_CAP);
    }
    value = cJSON_GetObjectItemCaseSensitive(root, "ai2");
    if (cJSON_IsString(value) && value->valuestring != NULL) {
        NormalizeAiSelection(value->valuestring, out->ai2, PDK_AI_NAME_CAP);
    }
    value = cJSON_GetObjectItemCaseSensitive(root, "roundTraceEnabled");
    if (cJSON_IsBool(value)) {
        out->roundTraceEnabled = cJSON_IsTrue(value) != 0;
    }

    cJSON_Delete(root);
}

bool SaveAppSettings(const AppSettings *settings, const char *path)
{
    cJSON *root;
    char ai1[PDK_AI_NAME_CAP];
    char ai2[PDK_AI_NAME_CAP];
    char *text;
    bool ok;

    /* aiProviders / cardScale / animationSpeed must not come back (AGENTS.md). */
    NormalizeAiSelection(settings->ai1, ai1, PDK_AI_NAME_CAP);
    NormalizeAiSelection(settings->ai2, ai2, PDK_AI_NAME_CAP);

    root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "playerName", settings->playerName);
    cJSON_AddNumberToObject(root, "masterVolume", settings->masterVolume);
    cJSON_AddNumberToObject(root, "windowWidth", settings->windowWidth);
    cJSON_AddNumberToObject(root, "windowHeight", settings->windowHeight);
    cJSON_AddStringToObject(root, "ai1", ai1);
    cJSON_AddStringToObject(root, "ai2", ai2);
    cJSON_AddBoolToObject(root, "roundTraceEnabled", settings->roundTraceEnabled);

    text = cJSON_Print(root);
    cJSON_Delete(root);
    if (text == NULL) {
        return false;
    }

    ok = WinFile_WriteTextFile(path != NULL ? path : "appsettings.json", text);
    cJSON_free(text);
    return ok;
}
