#include "graphics/win_compat.h"

#include "overlays/ReturnToMenuOverlay.h"

#include "app/App.h"
#include "graphics/d2d_c.h"
#include "ui/Widgets.h"

#include <stdlib.h>

enum { kButtonCapacity = 2 };

typedef struct ReturnToMenuOverlay {
    void *app;
    Button buttons[kButtonCapacity];
    int buttonCount;
    float elapsed;
} ReturnToMenuOverlay;

static const Rect kPanel = {400.0f, 226.0f, 480.0f, 256.0f};

static void ReturnToMenuOverlay_Update(void *user, float dt)
{
    ReturnToMenuOverlay *overlay = (ReturnToMenuOverlay *)user;

    overlay->elapsed += dt;
    ButtonGroup_UpdateAll(overlay->buttons, overlay->buttonCount, dt);
}

static void ReturnToMenuOverlay_Render(void *user, RenderContext *context)
{
    const ReturnToMenuOverlay *overlay = (const ReturnToMenuOverlay *)user;

    Widgets_BeginModal(context, &kPanel, overlay->elapsed);
    Widgets_DrawDialogBody(context, &kPanel, UI_ICON_BACK, THEME_GOLD, "返回主菜单？",
                           "当前这一局不会被记录。");
    ButtonGroup_DrawAll(context, overlay->buttons, overlay->buttonCount);
    Widgets_EndModal(context);
}

static bool ReturnToMenuOverlay_BlocksInputBelow(void *user)
{
    (void)user;
    return true;
}

static bool ReturnToMenuOverlay_OnMouseMove(void *user, float x, float y)
{
    ReturnToMenuOverlay *overlay = (ReturnToMenuOverlay *)user;

    ButtonGroup_UpdateHover(overlay->buttons, overlay->buttonCount, x, y);
    return true;
}

static bool ReturnToMenuOverlay_OnMouseDown(void *user, float x, float y)
{
    ReturnToMenuOverlay *overlay = (ReturnToMenuOverlay *)user;
    const int hit = ButtonGroup_Hit(overlay->buttons, overlay->buttonCount, x, y);

    if (hit == 0) {
        App_PlaySound(overlay->app, SOUND_CANCEL);
        App_ShowStart(overlay->app);
        return true;
    }
    if (hit == 1) {
        App_PlaySound(overlay->app, SOUND_RESUME);
        App_CloseTopOverlay(overlay->app);
        return true;
    }
    return true;
}

static void ReturnToMenuOverlay_Destroy(void *user)
{
    free(user);
}

static const OverlayVtbl kReturnToMenuOverlayVtbl = {
    .Update = ReturnToMenuOverlay_Update,
    .Render = ReturnToMenuOverlay_Render,
    .BlocksInputBelow = ReturnToMenuOverlay_BlocksInputBelow,
    .OnMouseMove = ReturnToMenuOverlay_OnMouseMove,
    .OnMouseDown = ReturnToMenuOverlay_OnMouseDown,
    .Destroy = ReturnToMenuOverlay_Destroy
};

Overlay ReturnToMenuOverlay_New(void *app)
{
    ReturnToMenuOverlay *overlay = (ReturnToMenuOverlay *)malloc(sizeof(ReturnToMenuOverlay));
    Overlay handle;

    if (overlay == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    overlay->app = app;
    overlay->buttons[0] = Button_Make(Rect_Make(kPanel.x + 60.0f, kPanel.y + 176.0f, 170.0f, 46.0f),
                                      "回主菜单", UI_BUTTON_SECONDARY);
    overlay->buttons[1] = Button_Make(Rect_Make(kPanel.x + 250.0f, kPanel.y + 176.0f, 170.0f, 46.0f),
                                      "继续游戏", UI_BUTTON_PRIMARY);
    overlay->buttonCount = kButtonCapacity;
    overlay->elapsed = 0.0f;
    handle.vtbl = &kReturnToMenuOverlayVtbl;
    handle.user = overlay;
    return handle;
}
