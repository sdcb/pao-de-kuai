#include "graphics/win_compat.h"

#include "overlays/ConfirmExitDialog.h"

#include "app/AppApi.h"
#include "graphics/d2d_c.h"
#include "ui/Widgets.h"

#include <stdlib.h>

enum { kButtonCapacity = 2 };

typedef struct ConfirmExitDialog {
    void *app;
    Button buttons[kButtonCapacity];
    int buttonCount;
    float elapsed;
} ConfirmExitDialog;

static const Rect kPanel = {400.0f, 226.0f, 480.0f, 256.0f};

static void ConfirmExitDialog_Update(void *user, float dt)
{
    ConfirmExitDialog *dialog = (ConfirmExitDialog *)user;

    dialog->elapsed += dt;
    ButtonGroup_UpdateAll(dialog->buttons, dialog->buttonCount, dt);
}

static void ConfirmExitDialog_Render(void *user, RenderContext *context)
{
    const ConfirmExitDialog *dialog = (const ConfirmExitDialog *)user;

    Widgets_BeginModal(context, &kPanel, dialog->elapsed);
    Widgets_DrawDialogBody(context, &kPanel, UI_ICON_EXIT, THEME_CINNABAR_LIGHT, "确认退出游戏？",
                           "设置会自动保存，下次再来。");
    ButtonGroup_DrawAll(context, dialog->buttons, dialog->buttonCount);
    Widgets_EndModal(context);
}

static bool ConfirmExitDialog_BlocksInputBelow(void *user)
{
    (void)user;
    return true;
}

static bool ConfirmExitDialog_OnMouseMove(void *user, float x, float y)
{
    ConfirmExitDialog *dialog = (ConfirmExitDialog *)user;

    ButtonGroup_UpdateHover(dialog->buttons, dialog->buttonCount, x, y);
    return true;
}

static bool ConfirmExitDialog_OnMouseDown(void *user, float x, float y)
{
    ConfirmExitDialog *dialog = (ConfirmExitDialog *)user;
    const int hit = ButtonGroup_Hit(dialog->buttons, dialog->buttonCount, x, y);

    if (hit == 0) {
        App_PlaySound(dialog->app, SOUND_CONFIRM);
        App_ConfirmExit(dialog->app);
        return true;
    }
    if (hit == 1) {
        App_PlaySound(dialog->app, SOUND_CANCEL);
        App_CloseTopOverlay(dialog->app);
        return true;
    }
    return true;
}

static void ConfirmExitDialog_Destroy(void *user)
{
    free(user);
}

static const OverlayVtbl kConfirmExitDialogVtbl = {
    .Update = ConfirmExitDialog_Update,
    .Render = ConfirmExitDialog_Render,
    .BlocksInputBelow = ConfirmExitDialog_BlocksInputBelow,
    .OnMouseMove = ConfirmExitDialog_OnMouseMove,
    .OnMouseDown = ConfirmExitDialog_OnMouseDown,
    .Destroy = ConfirmExitDialog_Destroy
};

Overlay ConfirmExitDialog_New(void *app)
{
    ConfirmExitDialog *dialog = (ConfirmExitDialog *)malloc(sizeof(ConfirmExitDialog));
    Overlay handle;

    if (dialog == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    dialog->app = app;
    dialog->buttons[0] = Button_Make(Rect_Make(kPanel.x + 60.0f, kPanel.y + 176.0f, 170.0f, 46.0f),
                                     "退出游戏", UI_BUTTON_DANGER);
    dialog->buttons[1] = Button_Make(Rect_Make(kPanel.x + 250.0f, kPanel.y + 176.0f, 170.0f, 46.0f),
                                     "再玩一会", UI_BUTTON_SECONDARY);
    dialog->buttonCount = kButtonCapacity;
    dialog->elapsed = 0.0f;
    handle.vtbl = &kConfirmExitDialogVtbl;
    handle.user = dialog;
    return handle;
}
