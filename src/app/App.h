#pragma once

/*
 * The application shell: window state, the current scene, the overlay stack, the device
 * resources and the live settings.
 *
 * Pure C.  What changed, and why:
 *   - The class members became plain values.  `graphics::RenderContext renderContext_` wrapped a
 *     C context, `audio::AudioEngine audio_` wrapped a C engine and `graphics::SpriteAtlas
 *     cardAtlas_` wrapped a C atlas; App owns the C types directly now, so the facades (and the
 *     "view" form of RenderContext the scene/overlay bridge needed) are off this path.
 *   - `stats::AppSettings settings_` (std::strings) became the C ::AppSettings, which is what
 *     appsettings.json already round-trips through.
 *   - The three private helpers that were only ever called with `this` are static functions taking
 *     `App *`; `TopOverlay` was a const method handing out a mutable overlay and is a plain
 *     accessor now.
 *   - The functions below are exactly the old `extern "C"` shims from the deleted app/AppApi.h,
 *     with their first parameter typed `App *` instead of `void *`.  The converted scenes and
 *     overlays store an opaque `void *app`, and C converts that to `App *` implicitly, so only
 *     their include line changed.
 *
 * Every App_* entry point takes a non-NULL `App *`; callers own the struct and initialise it with
 * App_Init (which zeroes it) before anything else.
 */

#include "app/ImeInput.h"
#include "audio/AudioEngine.h"
#include "audio/SoundIds.h"
#include "core/Geometry.h"
#include "core/KeyEvent.h"
#include "core/Overlay.h"
#include "core/Scene.h"
#include "core/SceneManager.h"
#include "game/RoundRecorder.h"
#include "graphics/D2DContext.h"
#include "graphics/SpriteAtlas.h"
#include "stats/AppSettings.h"

#include <stdbool.h>

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The status bar image and the AI bubbles are the deepest stack; a dialog under a toast fits. */
enum { APP_OVERLAY_CAPACITY = 8 };

typedef struct App {
    HWND hwnd;
    bool viewerMode;
    bool shouldQuit;
    bool audioLoaded;
    float sceneFade;
    RenderContext renderContext;
    AudioEngine audio;
    SpriteAtlas cardAtlas;
    SceneManager sceneManager;
    /* Overlays are stacked and swept in order every frame, so a fixed array is enough.  Each live
     * entry owns its implementation. */
    Overlay overlays[APP_OVERLAY_CAPACITY];
    int overlayCount;
    ImeInput ime;
    AppSettings settings;
    RoundRecorder recorder;
} App;

/* Zeroes every member and leaves the IME detached.  Offscreen renders into a WIC bitmap instead
 * of the window, which is what the screenshot tests use. */
void App_Init(App *app);
bool App_Initialize(App *app, HWND hwnd, bool viewerMode, bool offscreen);
void App_Shutdown(App *app);

void App_Update(App *app, float dt);
void App_Render(App *app);
void App_Resize(App *app, int width, int height);
bool App_OnMouseMove(App *app, float x, float y);
bool App_OnMouseDown(App *app, float x, float y);
bool App_OnMouseUp(App *app, float x, float y);
bool App_OnKeyDown(App *app, const KeyEvent *key);
/* `text` is a NUL-terminated wide string, replacing the old std::wstring parameter. */
bool App_OnText(App *app, const wchar_t *text);
/* Handles WM_IME_* while a text field has focus; returns false to fall back to DefWindowProc.
 * `lParam` is written through, exactly as the old LPARAM& parameter did. */
bool App_HandleImeMessage(App *app, UINT message, WPARAM wParam, LPARAM *lParam);
bool App_WantsTextInput(App *app);
void App_SyncIme(App *app);

void App_ShowStart(App *app);
void App_StartGame(App *app, bool mock);
void App_RestartCurrentGame(App *app);
void App_ShowStats(App *app);
void App_ShowSettings(App *app);
void App_ShowHelp(App *app);
/* The scene/overlay names the scene_viewer test fixture passes on its command line. */
void App_ShowViewerScene(App *app, const char *scene, const char *overlay, const char *mock);
void App_ChangeScene(App *app, Scene scene);

void App_PushOverlay(App *app, Overlay overlay);
void App_CloseTopOverlay(App *app);
void App_ClearOverlays(App *app);
void App_RequestClose(App *app);
void App_ConfirmExit(App *app);
bool App_ShouldQuit(App *app);

bool App_LoadGameResources(App *app);
bool App_LoadCardAtlas(App *app);
bool App_GameResourcesReady(App *app);
void App_ReleaseGameResources(App *app);

/* The C context the C scene and overlay render slots take, and the C atlas they draw from. */
RenderContext *App_RenderContext(App *app);
SpriteAtlas *App_CardAtlas(App *app);
RoundRecorder *App_Recorder(App *app);
bool App_ViewerMode(App *app);
HWND App_Hwnd(App *app);

void App_SaveSettings(App *app);
void App_UpdateWindowSize(App *app, int width, int height);
AppSettings *App_Settings(App *app);

/* ---- the C ABI the converted scenes and overlays drive ----------------- */

void App_PlaySound(App *app, int soundId);

void App_GetSettings(App *app, AppSettings *out);
/* Copies back into the live settings without persisting them. */
void App_ApplySettings(App *app, const AppSettings *in);
void App_SetMasterVolume(App *app, float volume);

void App_EnterGame(App *app);
void App_EnterStats(App *app);

/* The "欢迎回来" chip on the start screen; writes at most `cap` bytes including the terminator. */
void App_BuildWelcomeText(App *app, char *out, int cap);

#ifdef __cplusplus
}
#endif
