#include "graphics/win_compat.h"

#include "overlays/AboutOverlay.h"

#include "app/AppApi.h"
#include "graphics/d2d_c.h"
#include "ui/Widgets.h"

#include <stdlib.h>

enum { kButtonCapacity = 1 };

typedef struct AboutOverlay {
    void *app;
    Button buttons[kButtonCapacity];
    int buttonCount;
    float elapsed;
} AboutOverlay;

static const Rect kPanel = {300.0f, 96.0f, 680.0f, 528.0f};

/*
 * One "label + wrapping body" row of the credits.  The C++ version was a lambda capturing the
 * running y; C gets the same thing with an explicit cursor, which also makes a second caller
 * impossible to get wrong.
 */
typedef struct CreditsCursor {
    RenderContext *context;
    float y;
} CreditsCursor;

static void CreditsSection(CreditsCursor *cursor, const char *label, const char *body,
                           D2D1_COLOR_F color)
{
    TextStyle labelStyle = TextStyle_Label(14.0f, 600 /* SEMI_BOLD */);
    TextStyle bodyStyle = TextStyle_Label(15.0f, 400 /* NORMAL */);
    Rect labelRect;
    Rect bodyRect;
    Size measured;
    float width;

    bodyStyle.lineHeight = 23.0f;
    width = kPanel.width - 250.0f;

    labelRect = Rect_Make(kPanel.x + 70.0f, cursor->y + 1.0f, 110.0f, 24.0f);
    RenderContext_DrawTextUtf8(cursor->context, label, &labelRect, &labelStyle, THEME_GOLD);

    RenderContext_MeasureText(cursor->context, body, &bodyStyle, width, &measured);
    bodyRect = Rect_Make(kPanel.x + 180.0f, cursor->y, width, measured.height + 4.0f);
    RenderContext_DrawTextUtf8(cursor->context, body, &bodyRect, &bodyStyle, color);

    cursor->y += measured.height + 16.0f;
}

static void AboutOverlay_Update(void *user, float dt)
{
    AboutOverlay *overlay = (AboutOverlay *)user;

    overlay->elapsed += dt;
    ButtonGroup_UpdateAll(overlay->buttons, overlay->buttonCount, dt);
}

static void AboutOverlay_Render(void *user, RenderContext *context)
{
    const AboutOverlay *overlay = (const AboutOverlay *)user;
    CreditsCursor cursor;
    Point glowCenter;
    Point sealCenter;
    TextStyle titleStyle;
    TextStyle subtitleStyle;
    TextStyle thanksStyle;
    Rect titleRect;
    Rect subtitleRect;
    Rect thanksRect;
    Point starBox;
    Rect star;
    const char *thanks = "如果你喜欢这个项目，欢迎到 GitHub 给一个 star";
    Size thanksSize;
    float thanksLeft;

    Widgets_BeginModal(context, &kPanel, overlay->elapsed);
    Widgets_DrawPanel(context, &kPanel, NULL);
    glowCenter = Point_Make(640.0f, 150.0f);
    Widgets_DrawRadialGlow(context, glowCenter, 170.0f, WithAlpha(THEME_GOLD, 0.10f), 0.0f);

    sealCenter = Point_Make(640.0f, 150.0f);
    Widgets_DrawSeal(context, sealCenter, 64.0f, "快", THEME_CINNABAR, -5.0f, 40.0f);

    titleStyle = TextStyle_Centered(TextStyle_Kai(34.0f));
    titleRect = Rect_Make(kPanel.x, 194.0f, kPanel.width, 46.0f);
    RenderContext_DrawTextUtf8(context, "极客版跑得快", &titleRect, &titleStyle, THEME_GOLD_LIGHT);

    subtitleStyle = TextStyle_Centered(TextStyle_Label(15.5f, 400));
    subtitleRect = Rect_Make(kPanel.x, 242.0f, kPanel.width, 24.0f);
    RenderContext_DrawTextUtf8(context, "由 sdcb 开发，为妈妈做的一款单机跑得快", &subtitleRect,
                               &subtitleStyle, THEME_MUTED);

    Widgets_DrawHairline(context, kPanel.x + 80.0f, kPanel.x + kPanel.width - 80.0f, 282.0f, 0.45f);

    cursor.context = context;
    cursor.y = 302.0f;
    CreditsSection(&cursor, "开源地址", "https://github.com/sdcb/pao-de-kuai", THEME_GOLD_LIGHT);
    CreditsSection(&cursor, "使用技术",
                   "C、Win32、Direct2D、DirectWrite、WIC、Media Foundation、WASAPI、cJSON、doctest、CMake",
                   THEME_IVORY);
    CreditsSection(&cursor, "第三方许可", "cJSON / doctest 使用 MIT License", THEME_IVORY);

    thanksStyle = TextStyle_Label(15.0f, 400);
    RenderContext_MeasureText(context, thanks, &thanksStyle, 4096.0f, &thanksSize);
    thanksLeft = 640.0f - (thanksSize.width + 26.0f) * 0.5f;
    starBox = Point_Make(thanksLeft, 497.0f);
    star = Rect_Make(starBox.x, starBox.y, 18.0f, 18.0f);
    Icons_Draw(context, UI_ICON_STAR, &star, THEME_GOLD);
    thanksRect = Rect_Make(thanksLeft + 26.0f, 494.0f, thanksSize.width + 4.0f, 24.0f);
    RenderContext_DrawTextUtf8(context, thanks, &thanksRect, &thanksStyle, THEME_MUTED);

    ButtonGroup_DrawAll(context, overlay->buttons, overlay->buttonCount);
    Widgets_EndModal(context);
}

static bool AboutOverlay_BlocksInputBelow(void *user)
{
    (void)user;
    return true;
}

static bool AboutOverlay_OnMouseMove(void *user, float x, float y)
{
    AboutOverlay *overlay = (AboutOverlay *)user;

    ButtonGroup_UpdateHover(overlay->buttons, overlay->buttonCount, x, y);
    return true;
}

static bool AboutOverlay_OnMouseDown(void *user, float x, float y)
{
    AboutOverlay *overlay = (AboutOverlay *)user;

    if (ButtonGroup_Hit(overlay->buttons, overlay->buttonCount, x, y) >= 0) {
        App_PlaySound(overlay->app, SOUND_RESUME);
        App_CloseTopOverlay(overlay->app);
    }
    return true;
}

static void AboutOverlay_Destroy(void *user)
{
    free(user);
}

static const OverlayVtbl kAboutOverlayVtbl = {
    .Update = AboutOverlay_Update,
    .Render = AboutOverlay_Render,
    .BlocksInputBelow = AboutOverlay_BlocksInputBelow,
    .OnMouseMove = AboutOverlay_OnMouseMove,
    .OnMouseDown = AboutOverlay_OnMouseDown,
    .Destroy = AboutOverlay_Destroy
};

Overlay AboutOverlay_New(void *app)
{
    AboutOverlay *overlay = (AboutOverlay *)malloc(sizeof(AboutOverlay));
    Overlay handle;

    if (overlay == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    overlay->app = app;
    overlay->buttons[0] = Button_Make(Rect_Make(565.0f, 548.0f, 150.0f, 46.0f), "知道了",
                                      UI_BUTTON_PRIMARY);
    overlay->buttonCount = kButtonCapacity;
    overlay->elapsed = 0.0f;
    handle.vtbl = &kAboutOverlayVtbl;
    handle.user = overlay;
    return handle;
}
