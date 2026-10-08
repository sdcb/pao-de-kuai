#include "graphics/win_compat.h"

#include "overlays/TipOverlay.h"

#include "app/App.h"
#include "core/Str.h"
#include "graphics/d2d_c.h"
#include "ui/Widgets.h"

#include <stdlib.h>

enum { PDK_TIP_TEXT_CAP = 160 };

typedef struct TipOverlay {
    void *app;
    char text[PDK_TIP_TEXT_CAP];
    float elapsed;
} TipOverlay;

static void TipOverlay_Update(void *user, float dt)
{
    TipOverlay *tip = (TipOverlay *)user;

    tip->elapsed += dt;
}

static void TipOverlay_Render(void *user, RenderContext *context)
{
    const TipOverlay *tip = (const TipOverlay *)user;
    const float t = EaseOutCubic(Progress(tip->elapsed, 0.0f, 0.35f));
    ChipStyle chip = ChipStyle_Default();
    Rect rect;
    Rect shadow;
    Rect icon;
    Rect label;
    Point anchor;
    TextStyle style;

    RenderContext_PushOpacity(context, t);
    RenderContext_PushTranslation(context, 0.0f, (1.0f - t) * -12.0f);

    chip.fontSize = 16.0f;
    chip.height = 44.0f;
    chip.padX = 26.0f;
    chip.fill = WithAlpha(THEME_INK, 0.94f);
    chip.stroke = WithAlpha(THEME_GOLD, 0.6f);
    chip.text = THEME_IVORY;

    anchor = Point_Make(640.0f, 196.0f);
    rect = Widgets_ChipRect(context, anchor, UI_ANCHOR_CENTER, tip->text, &chip);
    rect.x -= 14.0f;
    rect.width += 28.0f;
    shadow = Rect_Make(rect.x + 4.0f, rect.y + 6.0f, rect.width - 8.0f, rect.height);
    RenderContext_DrawShadow(context, &shadow, 10.0f, ColorF_Make(0.0f, 0.0f, 0.0f, 0.5f));

    Widgets_DrawChipInRect(context, &rect, "", &chip);
    icon = Rect_Make(rect.x + 20.0f, rect.y + 13.0f, 18.0f, 18.0f);
    Icons_Draw(context, UI_ICON_SPARKLE, &icon, THEME_GOLD);

    style = TextStyle_Centered(TextStyle_Label(chip.fontSize, 400 /* NORMAL */));
    style.wrap = false;
    label = Rect_Make(rect.x + 28.0f, rect.y, rect.width - 28.0f, rect.height);
    RenderContext_DrawTextUtf8(context, tip->text, &label, &style, THEME_IVORY);

    RenderContext_PopTransform(context);
    RenderContext_PopOpacity(context);
}

static bool TipOverlay_BlocksInputBelow(void *user)
{
    (void)user;
    return false;
}

static bool TipOverlay_OnMouseDown(void *user, float x, float y)
{
    TipOverlay *tip = (TipOverlay *)user;

    (void)x;
    (void)y;
    App_CloseTopOverlay(tip->app);
    /* False, so the click that dismissed the tip still reaches the scene below it. */
    return false;
}

static void TipOverlay_Destroy(void *user)
{
    free(user);
}

static const OverlayVtbl kTipOverlayVtbl = {
    .Update = TipOverlay_Update,
    .Render = TipOverlay_Render,
    .BlocksInputBelow = TipOverlay_BlocksInputBelow,
    .OnMouseDown = TipOverlay_OnMouseDown,
    .Destroy = TipOverlay_Destroy
};

Overlay TipOverlay_New(void *app, const char *text)
{
    TipOverlay *tip = (TipOverlay *)malloc(sizeof(TipOverlay));
    Overlay handle;

    if (tip == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    tip->app = app;
    Str_CopyTo(tip->text, PDK_TIP_TEXT_CAP, text);
    tip->elapsed = 0.0f;
    handle.vtbl = &kTipOverlayVtbl;
    handle.user = tip;
    return handle;
}
