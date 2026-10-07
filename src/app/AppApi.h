#pragma once

/*
 * TEMPORARY C ABI over the still-C++ App -- DELETE WHEN APP ITSELF BECOMES C.
 *
 * The overlays are converting to C before App is, and the ones with buttons need to drive the app:
 * play a sound, close themselves, confirm the exit, go back to the main menu.  Those are the only
 * things they ask for, so this is a deliberately tiny facade with extern "C" definitions in
 * App.cpp -- a C translation unit cannot call a C++ member function, and rewriting App first would
 * mean converting three files in one step instead of one at a time.
 *
 * `app` is an opaque `App*`: C has no opinion about the type, which is the point.
 */

#include "audio/SoundIds.h"

#ifdef __cplusplus
extern "C" {
#endif

void App_PlaySound(void *app, int soundId);
void App_CloseTopOverlay(void *app);
void App_ConfirmExit(void *app);
void App_ShowStart(void *app);
void App_RestartCurrentGame(void *app);
void App_SaveSettings(void *app);

#ifdef __cplusplus
}
#endif
