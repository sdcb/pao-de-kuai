#include "graphics/win_compat.h"

#include "scenes/StartScene.h"

#include "app/App.h"
#include "graphics/d2d_c.h"
#include "overlays/AboutOverlay.h"
#include "rules/Card.h"
#include "scenes/SceneCommon.h"
#include "ui/Anim.h"
#include "ui/CardView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <math.h>
#include <stdlib.h>

enum { kButtonCapacity = 6 };
enum { kFanCardCount = 5 };
enum { kWelcomeCapacity = 160 };

typedef struct StartScene {
    void *app;
    Button buttons[kButtonCapacity];
    int buttonCount;
    char welcome[kWelcomeCapacity];
    float elapsed;
    Point mouse;
    Point parallax;
} StartScene;

static const float kMenuX = 842.0f;
static const float kMenuTop = 184.0f;
static const float kMenuWidth = 300.0f;
static const float kMenuHeight = 54.0f;
static const float kMenuGap = 14.0f;

/* 10 J Q K A: the top straight, a quiet nod to the rules on the title screen. */
static const Card kFanCards[kFanCardCount] = {{RANK_TEN, SUIT_HEARTS},
                                              {RANK_JACK, SUIT_CLUBS},
                                              {RANK_QUEEN, SUIT_DIAMONDS},
                                              {RANK_KING, SUIT_SPADES},
                                              {RANK_ACE, SUIT_HEARTS}};

static void StartScene_DrawCardFan(StartScene *scene, RenderContext *context, SpriteAtlas *atlas,
                                   float appear)
{
    const Point pivot = Point_Make(360.0f + scene->parallax.x * 14.0f,
                                   900.0f + scene->parallax.y * 8.0f);
    const float cardW = 116.0f;
    const float cardH = cardW * 1.4f;

    for (int i = 0; i < kFanCardCount; ++i) {
        const float index = (float)i - 2.0f;
        const float drift = sinf(scene->elapsed * 0.9f + (float)i * 0.8f);
        const float angle = (index * 12.5f + drift * 0.8f) * appear;
        const float radians = angle * 3.14159265f / 180.0f;
        const float reach = 378.0f + drift * 3.0f;
        const Point center = Point_Make(pivot.x + sinf(radians) * reach,
                                        pivot.y - cosf(radians) * reach + (1.0f - appear) * 60.0f);
        CardLook look = CardLook_Default();
        Rect rect = Rect_Make(center.x - cardW * 0.5f, center.y - cardH * 0.5f, cardW, cardH);

        look.rotation = angle;
        look.shadow = 1.2f;
        look.opacity = appear;
        CardView_DrawFace(context, atlas, kFanCards[i], &rect, &look);
    }
}

static void StartScene_DrawTitle(StartScene *scene, RenderContext *context)
{
    const float t = EaseOutCubic(Progress(scene->elapsed, 0.0f, 0.7f));
    Rect titleRect = Rect_Make(150.0f, 150.0f, 560.0f, 150.0f);
    TextStyle title = TextStyle_Kai(132.0f);
    Rect shadowRect = Rect_Make(titleRect.x + 3.0f, titleRect.y + 6.0f, titleRect.width,
                                titleRect.height);
    GradientStop titleStops[3];
    Rect subtitleRect = Rect_Make(156.0f, 312.0f, 560.0f, 30.0f);
    TextStyle subtitleStyle = TextStyle_Label(19.0f, 400);
    ChipStyle chip = ChipStyle_Default();
    Point chipAnchor = Point_Make(156.0f, 392.0f);

    RenderContext_PushOpacity(context, t);
    RenderContext_PushTranslation(context, 0.0f, (1.0f - t) * 18.0f);

    title.weight = 700; /* BOLD, matching ui::Kai */
    title.wrap = false;
    RenderContext_DrawTextUtf8(context, "跑得快", &shadowRect, &title,
                               WithAlpha(THEME_ROOM_DEEP, 0.7f));
    titleStops[0] = GradientStop_Make(0.0f, THEME_GOLD_LIGHT);
    titleStops[1] = GradientStop_Make(0.55f, THEME_GOLD);
    titleStops[2] = GradientStop_Make(1.0f, THEME_GOLD_DEEP);
    RenderContext_DrawTextUtf8Brush(context, "跑得快", &titleRect, &title,
                                    RenderContext_Linear(context,
                                                         Point_Make(0.0f, titleRect.y + 20.0f),
                                                         Point_Make(0.0f, titleRect.y + 140.0f),
                                                         titleStops, 3));

    Widgets_DrawSeal(context, Point_Make(582.0f, 196.0f), 62.0f, "极\n客", THEME_CINNABAR, -6.0f,
                     25.0f);

    RenderContext_DrawTextUtf8(context, "三人  ·  四十八张  ·  经典规则", &subtitleRect,
                               &subtitleStyle, THEME_MUTED);
    Widgets_DrawHairline(context, 150.0f, 620.0f, 356.0f, 0.5f);

    chip.fontSize = 15.5f;
    chip.height = 34.0f;
    chip.padX = 18.0f;
    chip.stroke = WithAlpha(THEME_GOLD, 0.45f);
    Widgets_DrawChip(context, chipAnchor, UI_ANCHOR_LEFT, scene->welcome, &chip);

    RenderContext_PopTransform(context);
    RenderContext_PopOpacity(context);
}

static void StartScene_OnEnter(void *user)
{
    StartScene *scene = (StartScene *)user;

    App_LoadCardAtlas(scene->app);
    /* The player name plus today's aggregate is a C++-side computation; the scene only displays
     * the finished chip text. */
    App_BuildWelcomeText(scene->app, scene->welcome, kWelcomeCapacity);
}

static void StartScene_Update(void *user, float dt)
{
    StartScene *scene = (StartScene *)user;
    const Point target = Point_Make((scene->mouse.x - 640.0f) / 640.0f,
                                    (scene->mouse.y - 360.0f) / 360.0f);

    scene->elapsed += dt;
    ButtonGroup_UpdateAll(scene->buttons, scene->buttonCount, dt);
    scene->parallax.x = Approach(scene->parallax.x, target.x, 3.0f, dt);
    scene->parallax.y = Approach(scene->parallax.y, target.y, 3.0f, dt);
}

static void StartScene_Render(void *user, RenderContext *context)
{
    StartScene *scene = (StartScene *)user;
    SpriteAtlas *atlas = App_CardAtlas(scene->app);
    Point roomFocus;
    Point halo;
    Rect outerRing;
    Rect innerRing;
    GradientStop dividerStops[3];
    TextStyle footerStyle = TextStyle_Centered(TextStyle_Label(12.5f, 400));
    Rect footerRect = Rect_Make(0.0f, 680.0f, 1280.0f, 24.0f);

    if (!SpriteAtlas_Loaded(atlas)) {
        App_LoadCardAtlas(scene->app);
        atlas = App_CardAtlas(scene->app);
    }
    RenderContext_Clear(context, THEME_ROOM);

    roomFocus = Point_Make(430.0f + scene->parallax.x * 20.0f,
                           330.0f + scene->parallax.y * 14.0f);
    Widgets_DrawRoomBackground(context, roomFocus);

    /* Moon-like halo behind the title block. */
    halo = Point_Make(400.0f + scene->parallax.x * 8.0f, 330.0f + scene->parallax.y * 6.0f);
    Widgets_DrawRadialGlow(context, halo, 330.0f, WithAlpha(THEME_GOLD, 0.07f), 0.0f);
    outerRing = Rect_Make(halo.x - 262.0f, halo.y - 262.0f, 524.0f, 524.0f);
    innerRing = Rect_Make(halo.x - 250.0f, halo.y - 250.0f, 500.0f, 500.0f);
    RenderContext_StrokeEllipse(context, &outerRing, WithAlpha(THEME_GOLD, 0.10f), 1.0f);
    RenderContext_StrokeEllipse(context, &innerRing, WithAlpha(THEME_GOLD, 0.05f), 1.0f);

    StartScene_DrawCardFan(scene, context, atlas,
                           EaseOutCubic(Progress(scene->elapsed, 0.25f, 0.9f)));
    StartScene_DrawTitle(scene, context);

    /* Vertical divider between the title block and the menu. */
    dividerStops[0] = GradientStop_Make(0.0f, WithAlpha(THEME_GOLD, 0.0f));
    dividerStops[1] = GradientStop_Make(0.5f, WithAlpha(THEME_GOLD, 0.35f));
    dividerStops[2] = GradientStop_Make(1.0f, WithAlpha(THEME_GOLD, 0.0f));
    RenderContext_DrawLineBrush(
        context, Point_Make(770.0f, 170.0f), Point_Make(770.0f, 590.0f),
        RenderContext_Linear(context, Point_Make(770.0f, 170.0f), Point_Make(770.0f, 590.0f),
                             dividerStops, 3),
        1.0f);

    for (int i = 0; i < scene->buttonCount; ++i) {
        const float t = EaseOutCubic(Progress(scene->elapsed, 0.15f + (float)i * 0.06f, 0.5f));

        RenderContext_PushOpacity(context, t);
        RenderContext_PushTranslation(context, (1.0f - t) * 40.0f, 0.0f);
        Button_Draw(&scene->buttons[i], context);
        RenderContext_PopTransform(context);
        RenderContext_PopOpacity(context);
    }

    RenderContext_DrawTextUtf8(context, "使用 cJSON / doctest (MIT)", &footerRect, &footerStyle,
                               WithAlpha(THEME_FAINT, 0.9f));
}

static bool StartScene_OnMouseMove(void *user, float x, float y)
{
    StartScene *scene = (StartScene *)user;

    scene->mouse = Point_Make(x, y);
    ButtonGroup_UpdateHover(scene->buttons, scene->buttonCount, x, y);
    return true;
}

static bool StartScene_OnMouseDown(void *user, float x, float y)
{
    StartScene *scene = (StartScene *)user;
    const int hit = ButtonGroup_Hit(scene->buttons, scene->buttonCount, x, y);

    if (hit < 0) {
        return false;
    }
    App_PlaySound(scene->app, SOUND_BUTTON_CLICK);
    if (hit == 0) {
        App_StartGame(scene->app, false);
    } else if (hit == 1) {
        App_ShowStats(scene->app);
    } else if (hit == 2) {
        App_ShowSettings(scene->app);
    } else if (hit == 3) {
        App_ShowHelp(scene->app);
    } else if (hit == 4) {
        App_PushOverlay(scene->app, AboutOverlay_New(scene->app));
    } else if (hit == 5) {
        App_RequestClose(scene->app);
    }
    return true;
}

static void StartScene_Destroy(void *user)
{
    free(user);
}

static const SceneVtbl kStartSceneVtbl = {
    .OnEnter = StartScene_OnEnter,
    .Update = StartScene_Update,
    .Render = StartScene_Render,
    .OnMouseMove = StartScene_OnMouseMove,
    .OnMouseDown = StartScene_OnMouseDown,
    .Destroy = StartScene_Destroy
};

Scene StartScene_New(void *app)
{
    static const char *const kLabels[kButtonCapacity] = {"开始游戏", "积分统计", "设置",
                                                         "帮助",     "关于",     "退出"};
    StartScene *scene = (StartScene *)malloc(sizeof(StartScene));
    Scene handle;

    if (scene == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    scene->app = app;
    for (int i = 0; i < kButtonCapacity; ++i) {
        Button button = Button_Default();
        ButtonStyle style = UI_BUTTON_SECONDARY;

        if (i == 0) {
            style = UI_BUTTON_PRIMARY;
        } else if (i == kButtonCapacity - 1) {
            style = UI_BUTTON_GHOST;
        }
        button.rect = Rect_Make(kMenuX, kMenuTop + (float)i * (kMenuHeight + kMenuGap), kMenuWidth,
                                kMenuHeight);
        Str_CopyTo(button.text, PDK_BUTTON_TEXT_CAP, kLabels[i]);
        button.fontSize = 20.0f;
        button.style = style;
        scene->buttons[i] = button;
    }
    scene->buttons[0].fontSize = 22.0f;
    scene->buttonCount = kButtonCapacity;
    scene->welcome[0] = '\0';
    scene->elapsed = 0.0f;
    scene->mouse = Point_Make(640.0f, 360.0f);
    scene->parallax = Point_Make(0.0f, 0.0f);
    handle.vtbl = &kStartSceneVtbl;
    handle.user = scene;
    return handle;
}
