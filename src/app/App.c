#include "app/App.h"

#include "app/ImeInput.h"
#include "audio/SoundIds.h"
#include "core/Str.h"
#include "graphics/WicImageLoader.h"
#include "overlays/AboutOverlay.h"
#include "overlays/ConfirmExitDialog.h"
#include "overlays/InvalidMoveToast.h"
#include "overlays/ReturnToMenuOverlay.h"
#include "overlays/RoundResultOverlay.h"
#include "overlays/SettingsOverlay.h"
#include "overlays/TalkBubbleOverlay.h"
#include "overlays/TipOverlay.h"
#include "resources/ResourceIds.h"
#include "resources/ResourceLoader.h"
#include "scenes/GameScene.h"
#include "scenes/HelpScene.h"
#include "scenes/LoadingScene.h"
#include "scenes/StartScene.h"
#include "scenes/StatsScene.h"
#include "ui/Anim.h"
#include "ui/Theme.h"

#include <imm.h>
#include <string.h>

/* ---- the private helpers the class methods used to be ----------------- */

/* Appends to a fixed buffer whose contents are already NUL terminated.  Str_CopyTo writes its
 * source at the start of the buffer, so the offset has to be computed first. */
static void App_AppendText(char *out, int cap, const char *text)
{
    const int length = (int)strlen(out);

    if (length >= cap) {
        return;
    }
    Str_CopyTo(out + length, cap - length, text);
}

/* The old `Number`/`Signed` built a std::string; C has no stream formatting, so this writes the
 * same bytes into the caller's buffer. */
static void App_AppendNumber(char *out, int cap, int value)
{
    char digits[16];
    int count = 0;
    int length = (int)strlen(out);
    unsigned int magnitude = value < 0 ? (unsigned int)0 - (unsigned int)value : (unsigned int)value;

    if (length >= cap) {
        return;
    }
    do {
        digits[count++] = (char)('0' + (int)(magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0 && count < (int)sizeof(digits));
    if (value < 0 && length + 1 < cap) {
        out[length++] = '-';
    }
    while (count > 0 && length + 1 < cap) {
        out[length++] = digits[--count];
    }
    out[length] = '\0';
}

/* The old const method handed out a mutable overlay, exactly as the old vector<unique_ptr> did;
 * TopOverlay returns that same mutable pointer, which is what lets App_SyncIme stay const-correct
 * in spirit while still moving the caret. */
static Overlay *App_TopOverlay(App *app)
{
    return app->overlayCount > 0 ? &app->overlays[app->overlayCount - 1] : NULL;
}

void App_Init(App *app)
{
    memset(app, 0, sizeof(*app));
    AppSettings_Default(&app->settings);
    RenderContext_Init(&app->renderContext);
    AudioEngine_Init(&app->audio);
    SceneManager_Init(&app->sceneManager);
    ImeInput_Init(&app->ime);
    RoundRecorder_Init(&app->recorder);
}

bool App_Initialize(App *app, HWND hwnd, bool viewerMode, bool offscreen)
{
    app->hwnd = hwnd;
    app->viewerMode = viewerMode;
    ImeInput_Attach(&app->ime, app->hwnd);
    LoadAppSettings(NULL, &app->settings);
    AudioEngine_Initialize(&app->audio);
    AudioEngine_SetMasterVolume(&app->audio, app->settings.masterVolume);
    if (!RenderContext_Initialize(&app->renderContext, app->hwnd, offscreen)) {
        return false;
    }
    if (!app->viewerMode) {
        App_ShowStart(app);
    }
    return true;
}

void App_Shutdown(App *app)
{
    App_ClearOverlays(app);
    SceneManager_Release(&app->sceneManager);
    SpriteAtlas_Reset(&app->cardAtlas);
    RenderContext_Shutdown(&app->renderContext);
    AudioEngine_Destroy(&app->audio);
}

void App_Update(App *app, float dt)
{
    Scene *scene;

    app->sceneFade = dt / 0.28f < app->sceneFade ? app->sceneFade - dt / 0.28f : 0.0f;
    scene = SceneManager_Current(&app->sceneManager);
    if (scene != NULL) {
        Scene_Update(scene, dt);
    }
    for (int i = 0; i < app->overlayCount; ++i) {
        Overlay_Update(&app->overlays[i], dt);
    }
    /* Drop the expired ones (the toasts and the AI bubbles), keeping the rest in order.  The old
     * code told those two types apart with dynamic_cast; now the vtable answers for them, which
     * also means a future expiring overlay needs no change here. */
    for (int i = 0; i < app->overlayCount;) {
        if (!Overlay_Expired(&app->overlays[i])) {
            ++i;
            continue;
        }
        Overlay_Release(&app->overlays[i]);
        for (int j = i + 1; j < app->overlayCount; ++j) {
            app->overlays[j - 1] = app->overlays[j];
        }
        --app->overlayCount;
    }
    App_SyncIme(app);
}

bool App_WantsTextInput(App *app)
{
    const Overlay *top = App_TopOverlay(app);

    return top != NULL && Overlay_WantsTextInput(top);
}

void App_SyncIme(App *app)
{
    const bool wants = App_WantsTextInput(app);
    Overlay *top = App_TopOverlay(app);
    Rect caret;

    ImeInput_SetEnabled(&app->ime, wants);
    if (wants && top != NULL && Overlay_TextCaretRect(top, &caret)) {
        ImeInput_SetCaret(&app->ime, &caret, &app->renderContext.transform);
    }
}

bool App_OnKeyDown(App *app, const KeyEvent *key)
{
    Overlay *top = App_TopOverlay(app);

    return top != NULL && Overlay_OnKeyDown(top, key);
}

bool App_OnText(App *app, const wchar_t *text)
{
    Overlay *top = App_TopOverlay(app);

    return top != NULL && Overlay_OnText(top, text);
}

bool App_HandleImeMessage(App *app, UINT message, WPARAM wParam, LPARAM *lParam)
{
    WStr text;
    int cursor = 0;

    (void)wParam;
    if (!App_WantsTextInput(app)) {
        return false;
    }
    switch (message) {
    case WM_IME_SETCONTEXT:
        /* The field draws the composition string itself; keep only the candidate list. */
        *lParam &= ~(LPARAM)ISC_SHOWUICOMPOSITIONWINDOW;
        return false;
    case WM_IME_STARTCOMPOSITION:
        App_SyncIme(app);
        return true;
    case WM_IME_COMPOSITION:
        WStr_Init(&text);
        if ((*lParam & GCS_RESULTSTR) != 0 && ImeInput_ReadResult(&app->ime, &text) &&
            text.len > 0) {
            Overlay_OnImeComposition(App_TopOverlay(app), L"", 0);
            Overlay_OnText(App_TopOverlay(app), WStr_CStr(&text));
        }
        cursor = 0;
        WStr_Clear(&text);
        if ((*lParam & GCS_COMPSTR) != 0 && ImeInput_ReadComposition(&app->ime, &text, &cursor)) {
            Overlay_OnImeComposition(App_TopOverlay(app), WStr_CStr(&text), cursor);
        }
        WStr_Free(&text);
        App_SyncIme(app);
        return true;
    case WM_IME_ENDCOMPOSITION:
        Overlay_OnImeComposition(App_TopOverlay(app), L"", 0);
        return true;
    default:
        return false;
    }
}

void App_Render(App *app)
{
    Scene *scene = SceneManager_Current(&app->sceneManager);

    RenderContext_BeginFrame(&app->renderContext);
    if (scene != NULL) {
        Scene_Render(scene, &app->renderContext);
    } else {
        RenderContext_Clear(&app->renderContext, ColorF_Make(0.03f, 0.18f, 0.13f, 1.0f));
    }
    for (int i = 0; i < app->overlayCount; ++i) {
        Overlay_Render(&app->overlays[i], &app->renderContext);
    }
    if (app->sceneFade > 0.0f) {
        const float t = app->sceneFade * app->sceneFade * (3.0f - 2.0f * app->sceneFade);
        const Rect full = Rect_Make(0.0f, 0.0f, LogicalWidth, LogicalHeight);

        RenderContext_FillRect(&app->renderContext, &full,
                               ColorF_Make(0.012f, 0.047f, 0.035f, t));
    }
    if (!RenderContext_EndFrame(&app->renderContext)) {
        App_ReleaseGameResources(app);
        if (scene != NULL) {
            Scene_OnD2DResourcesLost(scene);
        }
    }
}

void App_Resize(App *app, int width, int height)
{
    RenderContext_Resize(&app->renderContext, width, height);
    App_UpdateWindowSize(app, width, height);
}

bool App_OnMouseMove(App *app, float x, float y)
{
    Scene *scene;

    for (int i = app->overlayCount - 1; i >= 0; --i) {
        if (Overlay_OnMouseMove(&app->overlays[i], x, y) ||
            Overlay_BlocksInputBelow(&app->overlays[i])) {
            return true;
        }
    }
    scene = SceneManager_Current(&app->sceneManager);
    return scene != NULL && Scene_OnMouseMove(scene, x, y);
}

bool App_OnMouseDown(App *app, float x, float y)
{
    Scene *scene;

    for (int i = app->overlayCount - 1; i >= 0; --i) {
        Overlay *overlay = &app->overlays[i];

        if (Overlay_OnMouseDown(overlay, x, y)) {
            return true;
        }
        if (Overlay_BlocksInputBelow(overlay)) {
            return true;
        }
    }
    scene = SceneManager_Current(&app->sceneManager);
    return scene != NULL && Scene_OnMouseDown(scene, x, y);
}

bool App_OnMouseUp(App *app, float x, float y)
{
    Scene *scene;

    for (int i = app->overlayCount - 1; i >= 0; --i) {
        if (Overlay_OnMouseUp(&app->overlays[i], x, y) ||
            Overlay_BlocksInputBelow(&app->overlays[i])) {
            return true;
        }
    }
    scene = SceneManager_Current(&app->sceneManager);
    return scene != NULL && Scene_OnMouseUp(scene, x, y);
}

/* ---- the scene and overlay entry points the scenes and Window drive --- */

void App_PlaySound(App *app, int soundId)
{
    AudioEngine_Play(&app->audio, soundId);
}

void App_CloseTopOverlay(App *app)
{
    if (app->overlayCount > 0) {
        Overlay_Release(&app->overlays[app->overlayCount - 1]);
        --app->overlayCount;
    }
}

void App_ConfirmExit(App *app)
{
    App_SaveSettings(app);
    app->shouldQuit = true;
    if (app->hwnd != NULL) {
        DestroyWindow(app->hwnd);
    }
}

void App_SaveSettings(App *app)
{
    SaveAppSettings(&app->settings, NULL);
}

HWND App_Hwnd(App *app)
{
    return app->hwnd;
}

void App_GetSettings(App *app, AppSettings *out)
{
    *out = app->settings;
}

void App_ApplySettings(App *app, const AppSettings *in)
{
    app->settings = *in;
}

void App_SetMasterVolume(App *app, float volume)
{
    AudioEngine_SetMasterVolume(&app->audio, volume);
}

void App_PushOverlay(App *app, Overlay overlay)
{
    if (app->overlayCount >= APP_OVERLAY_CAPACITY || overlay.vtbl == NULL) {
        Overlay_Release(&overlay);
        return;
    }
    app->overlays[app->overlayCount++] = overlay;
}

void App_ShowHelp(App *app)
{
    App_ChangeScene(app, HelpScene_New(app));
}

void App_ShowSettings(App *app)
{
    App_PushOverlay(app, SettingsOverlay_New(app));
}

void App_ShowStats(App *app)
{
    App_ChangeScene(app, StatsScene_New(app));
}

void App_StartGame(App *app, bool mock)
{
    if (mock || App_GameResourcesReady(app)) {
        App_ChangeScene(app, GameScene_New(app, mock, false));
    } else {
        App_ChangeScene(app, LoadingScene_New(app, LOADING_TARGET_GAME));
    }
}

void App_RequestClose(App *app)
{
    if (app->viewerMode) {
        App_ConfirmExit(app);
        return;
    }
    App_PushOverlay(app, ConfirmExitDialog_New(app));
    AudioEngine_Play(&app->audio, SOUND_PAUSE);
}

bool App_LoadGameResources(App *app)
{
    if (!app->audioLoaded) {
        AudioEngine_SetMasterVolume(&app->audio, app->settings.masterVolume);
        AudioEngine_LoadAllFromResources(&app->audio);
        app->audioLoaded = true;
    }
    return App_LoadCardAtlas(app);
}

/* The loading screen's two exits.  Both target scenes are C, so this is just the construction
 * that a C translation unit (LoadingScene.c) cannot name. */
void App_EnterGame(App *app)
{
    App_ChangeScene(app, GameScene_New(app, false, false));
}

void App_EnterStats(App *app)
{
    App_ShowStats(app);
}

SpriteAtlas *App_CardAtlas(App *app)
{
    return &app->cardAtlas;
}

bool App_LoadCardAtlas(App *app)
{
    ByteBuffer bytes;
    SpriteAtlas *atlas = &app->cardAtlas;

    if (SpriteAtlas_Loaded(atlas)) {
        return true;
    }
    RenderContext_EnsureDeviceResources(&app->renderContext);
    ByteBuffer_Init(&bytes);
    if (!LoadResourceBytes(IDR_POKER_CARDS, RT_RCDATA, NULL, &bytes)) {
        return false;
    }
    SpriteAtlas_SetBitmap(
        atlas, WicImageLoader_LoadBitmapFromMemory(RenderContext_Target(&app->renderContext),
                                                   app->renderContext.wicFactory, bytes.data,
                                                   bytes.size, 1.0f));
    /* Pre-downsampled copies, exactly like the old `for (float scale : {0.5f, 0.25f})`. */
    for (int i = 0; i < 2; ++i) {
        const float scale = i == 0 ? 0.5f : 0.25f;

        SpriteAtlas_AddLevel(
            atlas, WicImageLoader_LoadBitmapFromMemory(RenderContext_Target(&app->renderContext),
                                                       app->renderContext.wicFactory, bytes.data,
                                                       bytes.size, scale),
            scale);
    }
    ByteBuffer_Free(&bytes);
    return SpriteAtlas_Loaded(atlas);
}

bool App_GameResourcesReady(App *app)
{
    return app->audioLoaded && SpriteAtlas_Loaded(&app->cardAtlas);
}

RoundRecorder *App_Recorder(App *app)
{
    return &app->recorder;
}

bool App_ViewerMode(App *app)
{
    return app->viewerMode;
}

/*
 * The old code composed the "欢迎回来" chip out of std::string plus core::AppendNumber.  C has no
 * stream formatting, so the same bytes are appended into the caller's fixed buffer; the chip is a
 * player name plus a signed score, which PDK_PLAYER_NAME_CAP plus a few dozen characters bounds.
 */
void App_BuildWelcomeText(App *app, char *out, int cap)
{
    StatStore store;
    StatSummary today;
    char date[PDK_DATE_KEY_CAP];
    char number[24];

    if (cap <= 0) {
        return;
    }
    out[0] = '\0';
    StatStore_Init(&store, NULL);
    TodayDateKey(date, PDK_DATE_KEY_CAP);
    StatSummary_Init(&today);
    StatStore_SummarizeDay(&store, date, &today);

    if (app->settings.playerName[0] != '\0') {
        Str_CopyTo(out, cap, app->settings.playerName);
        App_AppendText(out, cap, "\xEF\xBC\x8C\xE6\xAC\xA2\xE8\xBF\x8E\xE5\x9B\x9E\xE6\x9D\xA5");
    } else {
        Str_CopyTo(out, cap, "\xE6\xAC\xA2\xE8\xBF\x8E\xE5\x9B\x9E\xE6\x9D\xA5");
    }
    if (today.rounds > 0) {
        App_AppendText(out, cap, "  \xC2\xB7  \xE4\xBB\x8A\xE6\x97\xA5 ");
        if (today.scores[0] > 0) {
            App_AppendText(out, cap, "+");
        }
        App_AppendNumber(number, (int)sizeof(number), today.scores[0]);
        App_AppendText(out, cap, number);
        App_AppendText(out, cap, " \xE5\x88\x86");
    }
}

void App_ShowStart(App *app)
{
    App_ChangeScene(app, StartScene_New(app));
}

void App_RestartCurrentGame(App *app)
{
    Scene *scene = SceneManager_Current(&app->sceneManager);

    /* The scene decides for itself now: only the game scene restarts a round, and it reports that
     * through the vtable instead of App casting to a concrete type. */
    if (scene != NULL && Scene_RestartRound(scene)) {
        App_ClearOverlays(app);
        return;
    }
    App_StartGame(app, false);
}

void App_ShowViewerScene(App *app, const char *scene, const char *overlay, const char *mock)
{
    if (scene != NULL && strcmp(scene, "game") == 0 && mock != NULL && strcmp(mock, "midgame") == 0) {
        App_ChangeScene(app, GameScene_New(app, true, true));
    } else if (scene != NULL && strcmp(scene, "game") == 0) {
        App_StartGame(app, true);
    } else if (scene != NULL && strcmp(scene, "stats") == 0) {
        App_ShowStats(app);
    } else if (scene != NULL && strcmp(scene, "settings") == 0) {
        App_ShowStart(app);
        App_ShowSettings(app);
    } else if (scene != NULL && strcmp(scene, "help") == 0) {
        App_ShowHelp(app);
    } else if (scene != NULL && strcmp(scene, "loading") == 0) {
        App_ChangeScene(app, LoadingScene_New(app, LOADING_TARGET_GAME));
    } else {
        App_ShowStart(app);
    }

    if (overlay == NULL) {
        return;
    }
    if (strcmp(overlay, "confirm-exit") == 0) {
        App_PushOverlay(app, ConfirmExitDialog_New(app));
    } else if (strcmp(overlay, "about") == 0) {
        App_PushOverlay(app, AboutOverlay_New(app));
    } else if (strcmp(overlay, "tip") == 0) {
        App_PushOverlay(app, TipOverlay_New(app, "\xE6\x8E\xA8\xE8\x8D\x90\xE5\x85\x88\xE8\xB5\xB0"
                                                   "\xE9\xA1\xBA\xE5\xAD\x90\xEF\xBC\x8C\xE5\xB0\x91"
                                                   "\xE7\x95\x99\xE6\x95\xA3\xE7\x89\x8C"));
    } else if (strcmp(overlay, "invalid") == 0) {
        App_PushOverlay(app, InvalidMoveToast_New(
                                  "\xE7\x89\x8C\xE5\x9E\x8B\xE6\x88\x96\xE7\x82\xB9\xE6\x95\xB0"
                                  "\xE5\x8E\x8B\xE4\xB8\x8D\xE8\xBF\x87\xE4\xB8\x8A\xE5\xAE\xB6"));
    } else if (strcmp(overlay, "talk") == 0) {
        App_PushOverlay(app, TalkBubbleOverlay_New(
                                  PLAYER_AI1, "\xE5\x93\x87\xEF\xBC\x8C\xE6\x9D\x8E\xE5\xA7\x90"
                                              "\xE4\xBD\xA0\xE5\xA4\xAA\xE5\xBC\xBA\xE4\xBA\x86"
                                              "\xEF\xBC\x81"));
    } else if (strcmp(overlay, "return-menu") == 0) {
        App_PushOverlay(app, ReturnToMenuOverlay_New(app));
    } else if (strcmp(overlay, "result-win") == 0) {
        RoundRecord record;
        BombScoreEvent bomb;

        RoundRecord_Init(&record);
        record.winner = PLAYER_HUMAN;
        Str_CopyTo(record.playerName, PDK_PLAYER_NAME_CAP, app->settings.playerName);
        record.scores[0] = 18;
        record.scores[1] = -8;
        record.scores[2] = -10;
        record.remainingCards[0] = 0;
        record.remainingCards[1] = 8;
        record.remainingCards[2] = 10;
        bomb.by = PLAYER_HUMAN;
        bomb.score = 20;
        bomb.beaten = false;
        RoundRecord_AddBomb(&record, bomb);
        App_PushOverlay(app, RoundResultOverlay_New(app, &record));
    }
}

void App_ChangeScene(App *app, Scene scene)
{
    App_ClearOverlays(app);
    app->sceneFade = 1.0f;
    SceneManager_Change(&app->sceneManager, scene);
}

void App_ClearOverlays(App *app)
{
    for (int i = 0; i < app->overlayCount; ++i) {
        Overlay_Release(&app->overlays[i]);
    }
    app->overlayCount = 0;
}

bool App_ShouldQuit(App *app)
{
    return app->shouldQuit;
}

void App_ReleaseGameResources(App *app)
{
    SpriteAtlas_Reset(&app->cardAtlas);
}

AppSettings *App_Settings(App *app)
{
    return &app->settings;
}

void App_UpdateWindowSize(App *app, int width, int height)
{
    app->settings.windowWidth = width > 1280 ? width : 1280;
    app->settings.windowHeight = height > 720 ? height : 720;
}

RenderContext *App_RenderContext(App *app)
{
    return &app->renderContext;
}
