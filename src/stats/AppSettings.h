#pragma once

/*
 * Persistent application settings (appsettings.json).
 *
 * Pure C.  The path and player name used to be std::string; both are fixed
 * buffers now.  PDK_PLAYER_NAME_CAP is deliberately generous (a player name is a
 * display string typed into one text field) and NormalizeAiSelection clamps the
 * AI selection, so nothing here can overflow.
 *
 * The JSON schema is frozen (AGENTS.md "资源与数据策略"): playerName,
 * masterVolume, windowWidth, windowHeight, ai1, ai2, roundTraceEnabled.  The
 * removed fields (cardScale, animationSpeed, aiProviders) must stay absent, and
 * unknown values for ai1/ai2 normalise to "basic".
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PDK_PLAYER_NAME_CAP = 64 };
enum { PDK_AI_NAME_CAP = 16 };

typedef struct AppSettings {
    char playerName[PDK_PLAYER_NAME_CAP];
    float masterVolume;
    int windowWidth;
    int windowHeight;
    /* Local AI strategy per seat: "basic" or "strong". */
    char ai1[PDK_AI_NAME_CAP];
    char ai2[PDK_AI_NAME_CAP];
    bool roundTraceEnabled;
} AppSettings;

/* The defaults a missing or unreadable file produces. */
void AppSettings_Default(AppSettings *settings);
void AppSettings_SetPlayerName(AppSettings *settings, const char *name);

/* Writes "strong" or "basic" into `out`; anything unknown becomes "basic". */
void NormalizeAiSelection(const char *value, char *out, int cap);

/* `path` may be NULL for the default "appsettings.json".  A missing or malformed
 * file leaves `out` at its defaults, exactly like the previous implementation. */
void LoadAppSettings(const char *path, AppSettings *out);
bool SaveAppSettings(const AppSettings *settings, const char *path);

#ifdef __cplusplus
}
#endif
