#pragma once

/*
 * TEMPORARY C ABI over the still-C++ App -- DELETE WHEN APP ITSELF BECOMES C.
 *
 * The overlays are converting to C before App is, and the ones with buttons need to drive the app:
 * play a sound, close themselves, confirm the exit, go back to the main menu.  The settings modal
 * also needs to read and write the live settings.  Those are the only things they ask for, so this
 * is a deliberately tiny facade with extern "C" definitions in App.cpp -- a C translation unit
 * cannot call a C++ member function, and rewriting App first would mean converting three files in
 * one step instead of one at a time.
 *
 * `app` is an opaque `App*`: C has no opinion about the type, which is the point.
 */

#include "audio/SoundIds.h"
#include "core/Overlay.h"
#include "game/RoundRecorder.h"
#include "graphics/SpriteAtlas.h"
#include "stats/AppSettings.h"

#ifdef __cplusplus
extern "C" {
#endif

void App_PlaySound(void *app, int soundId);
void App_CloseTopOverlay(void *app);
void App_ConfirmExit(void *app);
void App_ShowStart(void *app);
void App_RestartCurrentGame(void *app);
void App_SaveSettings(void *app);

/* ---- the four small menu scenes -------------------------------------- */

/* Takes ownership of the handle, exactly like App::PushOverlay did. */
void App_PushOverlay(void *app, Overlay overlay);
void App_ShowHelp(void *app);
void App_ShowSettings(void *app);
void App_ShowStats(void *app);
void App_StartGame(void *app);
void App_RequestClose(void *app);
void App_LoadGameResources(void *app);

/*
 * A C translation unit cannot construct a scene type, and the loading screen finishes by handing
 * over to one of the two scenes that are still C++: these two are that hand-over.
 */
void App_EnterGame(void *app);
void App_EnterStats(void *app);

/* App::CardAtlas() returns the C++ SpriteAtlas facade; hand C its inner ::SpriteAtlas. */
SpriteAtlas *App_CardAtlas(void *app);
bool App_LoadCardAtlas(void *app);

/*
 * The three things only the game scene asks App for: whether the heavy resources (audio and card
 * atlas) are already loaded, the round recorder it appends a finished round to, and the viewer
 * flag that turns off both the AI controllers and record writing.
 */
bool App_GameResourcesReady(void *app);
RoundRecorder *App_Recorder(void *app);
bool App_ViewerMode(void *app);

/*
 * The "欢迎回来" chip on the start screen.  It needs the player name (which the C side could read
 * through App_GetSettings) and today's aggregate score, which lives behind the C++ StatStore
 * facade; composing the string here keeps that aggregation on the C++ side, where it is a single
 * call, and the scene just copies the finished text.
 */
void App_BuildWelcomeText(void *app, char *out, int cap);
/* The process's window, as an HWND.  `void *` keeps windows.h out of this header; the only user
 * is the settings modal, which hands it to the text field as its clipboard owner. */
void *App_Hwnd(void *app);

/* `App::Settings()` returns a C++ struct full of std::strings, so the copy in and out of the C
 * ::AppSettings goes through stats/CppCompat.h's ToCSettings / FromCSettings. */
void App_GetSettings(void *app, AppSettings *out);
/* Copies back into the live settings without persisting them. */
void App_ApplySettings(void *app, const AppSettings *in);
void App_SetMasterVolume(void *app, float volume);

#ifdef __cplusplus
}
#endif
