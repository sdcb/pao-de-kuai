#include "graphics/win_compat.h"

#include "overlays/InvalidMoveToast.h"

#include "core/Str.h"
#include "graphics/d2d_c.h"
#include "ui/Widgets.h"

#include <stdlib.h>

enum { PDK_TOAST_TEXT_CAP = 96 };

typedef struct InvalidMoveToast {
    char text[PDK_TOAST_TEXT_CAP];
    float elapsed;
} InvalidMoveToast;

static void InvalidMoveToast_Update(void *user, float dt)
{
    InvalidMoveToast *toast = (InvalidMoveToast *)user;

    toast->elapsed += dt;
}

static void InvalidMoveToast_Render(void *user, RenderContext *context)
{
    const InvalidMoveToast *toast = (const InvalidMoveToast *)user;
    const float in = EaseOutBackWith(Progress(toast->elapsed, 0.0f, 0.3f), 1.4f);
    const float alpha = Clamp01(toast->elapsed * 8.0f) * Clamp01((2.0f - toast->elapsed) / 0.4f);
    ChipStyle chip = ChipStyle_Default();
    Rect rect;
    Rect shadow;
    Rect icon;
    Rect label;
    Point anchor;
    TextStyle style;

    RenderContext_PushOpacity(context, alpha);
    RenderContext_PushTranslation(context, 0.0f, (1.0f - in) * -18.0f);

    chip.fontSize = 16.0f;
    chip.height = 42.0f;
    chip.padX = 24.0f;
    chip.fill = WithAlpha(THEME_CINNABAR_DEEP, 0.94f);
    chip.stroke = WithAlpha(THEME_CINNABAR_LIGHT, 0.75f);

    anchor = Point_Make(640.0f, 118.0f);
    rect = Widgets_ChipRect(context, anchor, UI_ANCHOR_CENTER, toast->text, &chip);
    rect.x -= 14.0f;
    rect.width += 28.0f;
    shadow = Rect_Make(rect.x + 4.0f, rect.y + 6.0f, rect.width - 8.0f, rect.height);
    RenderContext_DrawShadow(context, &shadow, 10.0f, ColorF_Make(0.0f, 0.0f, 0.0f, 0.55f));

    Widgets_DrawChipInRect(context, &rect, "", &chip);
    icon = Rect_Make(rect.x + 18.0f, rect.y + 11.0f, 20.0f, 20.0f);
    Icons_Draw(context, UI_ICON_ALERT, &icon, THEME_IVORY);

    style = TextStyle_Centered(TextStyle_Label(chip.fontSize, 600 /* SEMI_BOLD */));
    style.wrap = false;
    label = Rect_Make(rect.x + 28.0f, rect.y, rect.width - 28.0f, rect.height);
    RenderContext_DrawTextUtf8(context, toast->text, &label, &style, THEME_IVORY);

    RenderContext_PopTransform(context);
    RenderContext_PopOpacity(context);
}

static bool InvalidMoveToast_BlocksInputBelow(void *user)
{
    (void)user;
    return false;
}

static bool InvalidMoveToast_Expired(void *user)
{
    const InvalidMoveToast *toast = (const InvalidMoveToast *)user;

    return toast->elapsed > 2.0f;
}

static void InvalidMoveToast_Destroy(void *user)
{
    free(user);
}

static const OverlayVtbl kInvalidMoveToastVtbl = {
    .Update = InvalidMoveToast_Update,
    .Render = InvalidMoveToast_Render,
    .BlocksInputBelow = InvalidMoveToast_BlocksInputBelow,
    .Expired = InvalidMoveToast_Expired,
    .Destroy = InvalidMoveToast_Destroy
};

Overlay InvalidMoveToast_New(const char *text)
{
    InvalidMoveToast *toast = (InvalidMoveToast *)malloc(sizeof(InvalidMoveToast));
    Overlay handle;

    if (toast == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    Str_CopyTo(toast->text, PDK_TOAST_TEXT_CAP, text);
    toast->elapsed = 0.0f;
    handle.vtbl = &kInvalidMoveToastVtbl;
    handle.user = toast;
    return handle;
}
