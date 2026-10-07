#include "graphics/win_compat.h"

#include "scenes/LoadingScene.h"

#include "app/AppApi.h"
#include "core/Str.h"
#include "graphics/d2d_c.h"
#include "ui/Anim.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <stdlib.h>

/* The C++ member was a std::string; the longest text here is 18 UTF-8 bytes, so a fixed buffer
 * with Str_CopyTo covers it without any heap traffic. */
enum { kItemCapacity = 64 };

typedef struct LoadingScene {
    void *app;
    LoadingTarget target;
    float elapsed;
    float progress;
    float shownProgress;
    char item[kItemCapacity];
    bool loaded;
} LoadingScene;

static void LoadingScene_OnEnter(void *user)
{
    LoadingScene *scene = (LoadingScene *)user;

    Str_CopyTo(scene->item, kItemCapacity,
               scene->target == LOADING_TARGET_GAME ? "正在铺开牌桌，准备牌图与音效"
                                                    : "正在整理战绩");
    scene->progress = 0.08f;
}

static void LoadingScene_Update(void *user, float dt)
{
    LoadingScene *scene = (LoadingScene *)user;

    scene->elapsed += dt;
    scene->shownProgress = Approach(scene->shownProgress, scene->progress, 10.0f, dt);
    if (!scene->loaded && scene->elapsed > 0.12f) {
        scene->progress = 0.80f;
        if (scene->target == LOADING_TARGET_GAME) {
            App_LoadGameResources(scene->app);
            App_PlaySound(scene->app, SOUND_ROUND_START);
        }
        scene->loaded = true;
        scene->progress = 1.0f;
    }
    if (scene->loaded && scene->elapsed > 0.45f) {
        if (scene->target == LOADING_TARGET_GAME) {
            App_EnterGame(scene->app);
        } else {
            App_EnterStats(scene->app);
        }
    }
}

static void LoadingScene_Render(void *user, RenderContext *context)
{
    const LoadingScene *scene = (const LoadingScene *)user;
    TextStyle title;
    Rect titleRect = Rect_Make(0.0f, 230.0f, 1280.0f, 100.0f);
    GradientStop titleStops[3];
    Rect bar = Rect_Make(500.0f, 372.0f, 280.0f, 5.0f);
    TextStyle itemStyle;
    Rect itemRect = Rect_Make(0.0f, 392.0f, 1280.0f, 28.0f);
    Point sealCenter = Point_Make(782.0f, 256.0f);
    Point roomFocus = Point_Make(640.0f, 330.0f);
    Point glowCenter = Point_Make(640.0f, 300.0f);

    RenderContext_Clear(context, THEME_ROOM);
    Widgets_DrawRoomBackground(context, roomFocus);
    Widgets_DrawRadialGlow(context, glowCenter, 260.0f, WithAlpha(THEME_GOLD, 0.06f), 0.0f);

    title = TextStyle_Centered(TextStyle_Kai(76.0f));
    title.weight = 700; /* BOLD, matching ui::Kai */
    title.wrap = false;
    titleStops[0] = GradientStop_Make(0.0f, THEME_GOLD_LIGHT);
    titleStops[1] = GradientStop_Make(0.6f, THEME_GOLD);
    titleStops[2] = GradientStop_Make(1.0f, THEME_GOLD_DEEP);
    RenderContext_DrawTextUtf8Brush(context, "跑得快", &titleRect, &title,
                                    RenderContext_Linear(context, Point_Make(0.0f, 250.0f),
                                                         Point_Make(0.0f, 320.0f), titleStops, 3));
    Widgets_DrawSeal(context, sealCenter, 40.0f, "极\n客", THEME_CINNABAR, -6.0f, 16.0f);

    Widgets_DrawProgressBar(context, &bar, scene->shownProgress, scene->elapsed);
    itemStyle = TextStyle_Centered(TextStyle_Label(15.0f, 400));
    RenderContext_DrawTextUtf8(context, scene->item, &itemRect, &itemStyle, THEME_MUTED);
}

static void LoadingScene_Destroy(void *user)
{
    free(user);
}

static const SceneVtbl kLoadingSceneVtbl = {
    .OnEnter = LoadingScene_OnEnter,
    .Update = LoadingScene_Update,
    .Render = LoadingScene_Render,
    .Destroy = LoadingScene_Destroy
};

Scene LoadingScene_New(void *app, LoadingTarget target)
{
    LoadingScene *scene = (LoadingScene *)malloc(sizeof(LoadingScene));
    Scene handle;

    if (scene == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    scene->app = app;
    scene->target = target;
    scene->elapsed = 0.0f;
    scene->progress = 0.0f;
    scene->shownProgress = 0.0f;
    scene->item[0] = '\0';
    scene->loaded = false;
    handle.vtbl = &kLoadingSceneVtbl;
    handle.user = scene;
    return handle;
}
