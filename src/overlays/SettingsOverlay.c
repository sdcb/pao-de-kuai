#include "graphics/win_compat.h"

#include "overlays/SettingsOverlay.h"

#include "app/App.h"
#include "audio/SoundIds.h"
#include "core/Str.h"
#include "graphics/d2d_c.h"
#include "stats/AppSettings.h"
#include "ui/Inputs.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <stdlib.h>

enum { kButtonCapacity = 2 };

typedef struct SettingsOverlay {
    void *app;
    AppSettings draft;
    float originalVolume;
    TextField nameField;
    Slider volume;
    Segmented ai1;
    Segmented ai2;
    Toggle trace;
    Button buttons[kButtonCapacity];
    int buttonCount;
    float elapsed;
} SettingsOverlay;

static const Rect kPanel = {330.0f, 104.0f, 620.0f, 512.0f};
static const float kLabelX = 330.0f + 48.0f;
static const float kControlX = 330.0f + 188.0f;
static const float kControlRight = 330.0f + 620.0f - 48.0f;
static const float kRowName = 104.0f + 140.0f;
static const float kRowVolume = 104.0f + 212.0f;
static const float kRowAi1 = 104.0f + 280.0f;
static const float kRowAi2 = 104.0f + 342.0f;
static const float kRowTrace = 104.0f + 404.0f;

/* The old TrimSpaces returned a std::string; this trims in place and returns the first
 * non-space character, so the caller can keep using `text` directly. */
static char *TrimSpaces(char *text)
{
    int start = 0;
    int end;

    while (text[start] == ' ' || text[start] == '\t') {
        ++start;
    }
    end = start;
    while (text[end] != '\0') {
        ++end;
    }
    while (end > start && (text[end - 1] == ' ' || text[end - 1] == '\t')) {
        --end;
    }
    text[end] = '\0';
    return text + start;
}

static void DrawRowLabel(RenderContext *context, const char *text, float centerY)
{
    TextStyle style = TextStyle_Label(17.0f, 400 /* NORMAL */);
    Rect rect;

    style.valign = 2; /* DWRITE_PARAGRAPH_ALIGNMENT_CENTER */
    style.wrap = false;
    rect = Rect_Make(kLabelX, centerY - 20.0f, 130.0f, 40.0f);
    RenderContext_DrawTextUtf8(context, text, &rect, &style, THEME_MUTED);
}

static void DrawRowNote(RenderContext *context, const char *text, float x, float centerY)
{
    TextStyle style = TextStyle_Label(14.5f, 400 /* NORMAL */);
    Rect rect;

    style.valign = 2; /* DWRITE_PARAGRAPH_ALIGNMENT_CENTER */
    style.wrap = false;
    rect = Rect_Make(x, centerY - 16.0f, kControlRight - x, 32.0f);
    RenderContext_DrawTextUtf8(context, text, &rect, &style, THEME_FAINT);
}

static const char *StrategyNote(int selected)
{
    return selected == 1 ? "会记牌、算牌" : "稳健直接";
}

/* The old build of the volume readout used core::AppendNumber on a std::string; the modal
 * redraws every frame, so this appends the digits into a fixed buffer instead of a Str (a Str
 * would mean a malloc/free pair per frame). */
static void AppendNumber(char *buffer, int cap, int value)
{
    char digits[16];
    int count = 0;
    int length = 0;
    unsigned int magnitude = value < 0 ? (unsigned int)0 - (unsigned int)value : (unsigned int)value;

    while (buffer[length] != '\0' && length + 1 < cap) {
        ++length;
    }
    do {
        digits[count++] = (char)('0' + (int)(magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0 && count < (int)sizeof(digits));
    if (value < 0 && length + 1 < cap) {
        buffer[length++] = '-';
    }
    while (count > 0 && length + 1 < cap) {
        buffer[length++] = digits[--count];
    }
    buffer[length] = '\0';
}

static void SettingsOverlay_Save(SettingsOverlay *overlay)
{
    char name[PDK_PLAYER_NAME_CAP];
    char *trimmed;
    AppSettings live;

    TextField_Utf8To(&overlay->nameField, name, PDK_PLAYER_NAME_CAP);
    trimmed = TrimSpaces(name);
    if (trimmed[0] != '\0') {
        Str_CopyTo(overlay->draft.playerName, PDK_PLAYER_NAME_CAP, trimmed);
    }
    overlay->draft.masterVolume = overlay->volume.value;
    Str_CopyTo(overlay->draft.ai1, PDK_AI_NAME_CAP,
               overlay->ai1.selected == 1 ? "strong" : "basic");
    Str_CopyTo(overlay->draft.ai2, PDK_AI_NAME_CAP,
               overlay->ai2.selected == 1 ? "strong" : "basic");
    overlay->draft.roundTraceEnabled = overlay->trace.on;
    /* Window size is tracked live by the app; keep whatever it is now. */
    App_GetSettings(overlay->app, &live);
    overlay->draft.windowWidth = live.windowWidth;
    overlay->draft.windowHeight = live.windowHeight;
    App_ApplySettings(overlay->app, &overlay->draft);
    App_SetMasterVolume(overlay->app, overlay->draft.masterVolume);
    App_SaveSettings(overlay->app);
    App_PlaySound(overlay->app, SOUND_CONFIRM);
    App_CloseTopOverlay(overlay->app);
}

static void SettingsOverlay_Cancel(SettingsOverlay *overlay)
{
    App_SetMasterVolume(overlay->app, overlay->originalVolume);
    App_PlaySound(overlay->app, SOUND_CANCEL);
    App_CloseTopOverlay(overlay->app);
}

static void SettingsOverlay_Update(void *user, float dt)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;

    overlay->elapsed += dt;
    TextField_Update(&overlay->nameField, dt);
    Slider_Update(&overlay->volume, dt);
    Segmented_Update(&overlay->ai1, dt);
    Segmented_Update(&overlay->ai2, dt);
    Toggle_Update(&overlay->trace, dt);
    ButtonGroup_UpdateAll(overlay->buttons, overlay->buttonCount, dt);
}

static void SettingsOverlay_Render(void *user, RenderContext *context)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;
    TextStyle title;
    TextStyle hint;
    TextStyle valueStyle;
    char percent[32];
    Rect rect;

    Widgets_BeginModal(context, &kPanel, overlay->elapsed);
    Widgets_DrawPanel(context, &kPanel, NULL);

    title = TextStyle_Kai(32.0f);
    rect = Rect_Make(kPanel.x + 44.0f, kPanel.y + 26.0f, 200.0f, 44.0f);
    RenderContext_DrawTextUtf8(context, "设置", &rect, &title, THEME_GOLD_LIGHT);
    hint = TextStyle_Label(14.0f, 400 /* NORMAL */);
    hint.align = 2;  /* DWRITE_TEXT_ALIGNMENT_TRAILING */
    hint.valign = 2; /* DWRITE_PARAGRAPH_ALIGNMENT_CENTER */
    hint.wrap = false;
    rect = Rect_Make(kControlRight - 260.0f, kPanel.y + 30.0f, 260.0f, 36.0f);
    RenderContext_DrawTextUtf8(context, "Enter 保存 · Esc 取消", &rect, &hint, THEME_FAINT);
    Widgets_DrawHairline(context, kPanel.x + 32.0f, kPanel.x + kPanel.width - 32.0f,
                         kPanel.y + 84.0f, 0.45f);

    DrawRowLabel(context, "玩家名", kRowName);
    TextField_Draw(&overlay->nameField, context);
    DrawRowLabel(context, "主音量", kRowVolume);
    Slider_Draw(&overlay->volume, context);
    percent[0] = '\0';
    AppendNumber(percent, (int)sizeof(percent), RoundToInt(overlay->volume.value * 100.0f));
    if (percent[0] != '\0') {
        int length = 0;

        while (percent[length] != '\0') {
            ++length;
        }
        if (length + 1 < (int)sizeof(percent)) {
            percent[length++] = '%';
            percent[length] = '\0';
        }
    }
    valueStyle = TextStyle_Label(17.0f, 600 /* SEMI_BOLD */);
    valueStyle.align = 2;  /* DWRITE_TEXT_ALIGNMENT_TRAILING */
    valueStyle.valign = 2; /* DWRITE_PARAGRAPH_ALIGNMENT_CENTER */
    valueStyle.wrap = false;
    rect = Rect_Make(kControlRight - 70.0f, kRowVolume - 16.0f, 70.0f, 32.0f);
    RenderContext_DrawTextUtf8(context, percent, &rect, &valueStyle, THEME_GOLD_LIGHT);

    DrawRowLabel(context, "AI1 策略", kRowAi1);
    Segmented_Draw(&overlay->ai1, context);
    DrawRowNote(context, StrategyNote(overlay->ai1.selected),
                overlay->ai1.rect.x + overlay->ai1.rect.width + 18.0f, kRowAi1);

    DrawRowLabel(context, "AI2 策略", kRowAi2);
    Segmented_Draw(&overlay->ai2, context);
    DrawRowNote(context, StrategyNote(overlay->ai2.selected),
                overlay->ai2.rect.x + overlay->ai2.rect.width + 18.0f, kRowAi2);

    DrawRowLabel(context, "复盘记录", kRowTrace);
    Toggle_Draw(&overlay->trace, context);
    DrawRowNote(context, "每局结束写入复盘 JSON",
                overlay->trace.rect.x + overlay->trace.rect.width + 18.0f, kRowTrace);

    ButtonGroup_DrawAll(context, overlay->buttons, overlay->buttonCount);
    Widgets_EndModal(context);
}

static bool SettingsOverlay_BlocksInputBelow(void *user)
{
    (void)user;
    return true;
}

static bool SettingsOverlay_OnMouseMove(void *user, float x, float y)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;

    TextField_UpdateHover(&overlay->nameField, x, y);
    TextField_OnMouseMove(&overlay->nameField, x, y);
    if (Slider_OnMouseMove(&overlay->volume, x, y)) {
        overlay->draft.masterVolume = overlay->volume.value;
        App_SetMasterVolume(overlay->app, overlay->draft.masterVolume);
    }
    Segmented_UpdateHover(&overlay->ai1, x, y);
    Segmented_UpdateHover(&overlay->ai2, x, y);
    Toggle_UpdateHover(&overlay->trace, x, y);
    ButtonGroup_UpdateHover(overlay->buttons, overlay->buttonCount, x, y);
    return true;
}

static bool SettingsOverlay_OnMouseDown(void *user, float x, float y)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;
    const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    int hit;

    if (TextField_OnMouseDown(&overlay->nameField, x, y, shift)) {
        return true;
    }
    if (Slider_OnMouseDown(&overlay->volume, x, y)) {
        overlay->draft.masterVolume = overlay->volume.value;
        App_SetMasterVolume(overlay->app, overlay->draft.masterVolume);
        return true;
    }
    if (Segmented_OnMouseDown(&overlay->ai1, x, y) ||
        Segmented_OnMouseDown(&overlay->ai2, x, y)) {
        App_PlaySound(overlay->app, SOUND_SELECT_CARD);
        return true;
    }
    if (Toggle_OnMouseDown(&overlay->trace, x, y)) {
        App_PlaySound(overlay->app, SOUND_BUTTON_CLICK);
        return true;
    }
    hit = ButtonGroup_Hit(overlay->buttons, overlay->buttonCount, x, y);
    if (hit == 0) {
        SettingsOverlay_Cancel(overlay);
    } else if (hit == 1) {
        SettingsOverlay_Save(overlay);
    }
    return true;
}

static bool SettingsOverlay_OnMouseUp(void *user, float x, float y)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;

    (void)x;
    (void)y;
    TextField_OnMouseUp(&overlay->nameField);
    if (Slider_OnMouseUp(&overlay->volume)) {
        App_PlaySound(overlay->app, SOUND_SELECT_CARD);
    }
    return true;
}

static bool SettingsOverlay_OnKeyDown(void *user, const KeyEvent *key)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;

    if (key->key == VK_ESCAPE) {
        SettingsOverlay_Cancel(overlay);
        return true;
    }
    if (key->key == VK_RETURN) {
        SettingsOverlay_Save(overlay);
        return true;
    }
    if (key->key == VK_TAB) {
        TextField_SetFocused(&overlay->nameField, !TextField_Focused(&overlay->nameField));
        return true;
    }
    TextField_OnKeyDown(&overlay->nameField, key);
    return true;
}

static bool SettingsOverlay_OnText(void *user, const wchar_t *text)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;
    /* Both entry points only read the buffer for the duration of the call, so the WStr can
     * borrow the NUL-terminated string the vtable hands over instead of copying it. */
    WStr wide;
    int length = 0;

    while (text[length] != L'\0') {
        ++length;
    }
    wide.data = (wchar_t *)text;
    wide.len = length;
    wide.cap = length + 1;
    TextField_Insert(&overlay->nameField, &wide);
    return true;
}

static bool SettingsOverlay_WantsTextInput(void *user)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;

    return TextField_Focused(&overlay->nameField);
}

static void SettingsOverlay_OnImeComposition(void *user, const wchar_t *text, int cursor)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;
    WStr wide;
    int length = 0;

    while (text[length] != L'\0') {
        ++length;
    }
    wide.data = (wchar_t *)text;
    wide.len = length;
    wide.cap = length + 1;
    TextField_SetComposition(&overlay->nameField, &wide, cursor);
}

static bool SettingsOverlay_TextCaretRect(void *user, Rect *caret)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;

    if (!TextField_Focused(&overlay->nameField)) {
        return false;
    }
    return TextField_CaretRect(&overlay->nameField, caret);
}

static void SettingsOverlay_Destroy(void *user)
{
    SettingsOverlay *overlay = (SettingsOverlay *)user;

    TextField_Destroy(&overlay->nameField);
    free(overlay);
}

static const OverlayVtbl kSettingsOverlayVtbl = {
    .Update = SettingsOverlay_Update,
    .Render = SettingsOverlay_Render,
    .BlocksInputBelow = SettingsOverlay_BlocksInputBelow,
    .OnMouseMove = SettingsOverlay_OnMouseMove,
    .OnMouseDown = SettingsOverlay_OnMouseDown,
    .OnMouseUp = SettingsOverlay_OnMouseUp,
    .OnKeyDown = SettingsOverlay_OnKeyDown,
    .OnText = SettingsOverlay_OnText,
    .WantsTextInput = SettingsOverlay_WantsTextInput,
    .OnImeComposition = SettingsOverlay_OnImeComposition,
    .TextCaretRect = SettingsOverlay_TextCaretRect,
    .Destroy = SettingsOverlay_Destroy
};

Overlay SettingsOverlay_New(void *app)
{
    SettingsOverlay *overlay = (SettingsOverlay *)malloc(sizeof(SettingsOverlay));
    Overlay handle;
    const char *const strategies[2] = {"基础", "强力"};
    const float buttonY = kPanel.y + kPanel.height - 72.0f;

    if (overlay == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }

    overlay->app = app;
    App_GetSettings(app, &overlay->draft);
    overlay->originalVolume = overlay->draft.masterVolume;
    overlay->elapsed = 0.0f;

    TextField_Init(&overlay->nameField);
    overlay->nameField.rect = Rect_Make(kControlX, kRowName - 23.0f, kControlRight - kControlX,
                                        46.0f);
    Str_CopyTo(overlay->nameField.placeholder, PDK_TEXT_FIELD_PLACEHOLDER_CAP, "输入你的名字");
    overlay->nameField.clipboardOwner = (HWND)App_Hwnd(app);
    TextField_SetUtf8(&overlay->nameField, overlay->draft.playerName);
    TextField_SetFocused(&overlay->nameField, true);

    Slider_Init(&overlay->volume);
    overlay->volume.rect = Rect_Make(kControlX + 10.0f, kRowVolume - 14.0f, 300.0f, 28.0f);
    overlay->volume.value = overlay->draft.masterVolume;

    Segmented_Init(&overlay->ai1);
    overlay->ai1.rect = Rect_Make(kControlX, kRowAi1 - 20.0f, 240.0f, 40.0f);
    Segmented_SetOptions(&overlay->ai1, strategies, 2);
    overlay->ai1.selected = overlay->draft.ai1[0] == 's' ? 1 : 0;
    overlay->ai1.slide = (float)overlay->ai1.selected;

    Segmented_Init(&overlay->ai2);
    overlay->ai2.rect = Rect_Make(kControlX, kRowAi2 - 20.0f, 240.0f, 40.0f);
    Segmented_SetOptions(&overlay->ai2, strategies, 2);
    overlay->ai2.selected = overlay->draft.ai2[0] == 's' ? 1 : 0;
    overlay->ai2.slide = (float)overlay->ai2.selected;

    Toggle_Init(&overlay->trace);
    overlay->trace.rect = Rect_Make(kControlX, kRowTrace - 15.0f, 54.0f, 30.0f);
    overlay->trace.on = overlay->draft.roundTraceEnabled;
    overlay->trace.t = overlay->trace.on ? 1.0f : 0.0f;

    overlay->buttons[0] = Button_Make(Rect_Make(kControlRight - 296.0f, buttonY, 140.0f, 46.0f),
                                      "取消", UI_BUTTON_SECONDARY);
    overlay->buttons[1] = Button_Make(Rect_Make(kControlRight - 140.0f, buttonY, 140.0f, 46.0f),
                                      "保存", UI_BUTTON_PRIMARY);
    overlay->buttonCount = kButtonCapacity;

    handle.vtbl = &kSettingsOverlayVtbl;
    handle.user = overlay;
    return handle;
}
