#include "graphics/win_compat.h"

#include "ui/Inputs.h"

#include "graphics/d2d_c.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

static const D2D1_COLOR_F kBlack = {0.0f, 0.0f, 0.0f, 1.0f};
static const D2D1_COLOR_F kWhite = {1.0f, 1.0f, 1.0f, 1.0f};

/* The old `constexpr float FieldPadX` and `CaretBlinkPeriod`, as the two
 * spellings a C translation unit can use in a constant expression. */
enum { kFieldPadX = 16 };
#define kCaretBlinkPeriod 1.06f

static Rect MakeRect(float x, float y, float width, float height)
{
    Rect r;

    r.x = x;
    r.y = y;
    r.width = width;
    r.height = height;
    return r;
}

static Rect Inset(const Rect *rect, float d)
{
    return MakeRect(rect->x + d, rect->y + d, rect->width - d * 2.0f, rect->height - d * 2.0f);
}

static float FieldPadX(void)
{
    return (float)kFieldPadX;
}

static float MaxF(float a, float b)
{
    return a > b ? a : b;
}

/* ---- text and clipboard helpers --------------------------------------- */

/* Replacements for the old std::wstring Utf8ToWide/WideToUtf8: they fill a WStr instead of
 * returning a std::wstring / std::string, so the caller owns the buffer. */
static void WideFromUtf8(const char *utf8, WStr *out)
{
    wchar_t *wide;

    WStr_Init(out);
    if (utf8 == NULL || utf8[0] == '\0') {
        return;
    }
    wide = PdkUtf8ToWide(utf8);
    if (wide != NULL) {
        WStr_Assign(out, wide);
        free(wide);
    }
}

static void Utf8FromWide(const wchar_t *text, Str *out)
{
    char *utf8;

    Str_Init(out);
    if (text == NULL || text[0] == L'\0') {
        return;
    }
    utf8 = PdkWideToUtf8(text);
    if (utf8 != NULL) {
        Str_Assign(out, utf8);
        free(utf8);
    }
}

static void CopyToClipboard(HWND owner, const WStr *text)
{
    size_t bytes;
    HGLOBAL memory;
    void *data;

    if (text == NULL || text->len <= 0 || !OpenClipboard(owner)) {
        return;
    }
    EmptyClipboard();
    bytes = ((size_t)text->len + 1) * sizeof(wchar_t);
    memory = GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (memory != NULL) {
        data = GlobalLock(memory);
        if (data != NULL) {
            memcpy(data, WStr_CStr(text), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) {
                GlobalFree(memory);
            }
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

/* Returns true when the clipboard held text; the caller owns `out` and frees it. */
static bool ReadClipboard(HWND owner, WStr *out)
{
    HANDLE handle;
    const wchar_t *data;

    WStr_Init(out);
    if (!OpenClipboard(owner)) {
        return false;
    }
    handle = GetClipboardData(CF_UNICODETEXT);
    if (handle != NULL) {
        data = (const wchar_t *)GlobalLock(handle);
        if (data != NULL) {
            WStr_Assign(out, data);
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return out->len > 0;
}

static TextStyle FieldStyle(void)
{
    TextStyle style = TextStyle_Default();

    style.size = 19.0f;
    style.wrap = false;
    return style;
}

static void DrawKnob(RenderContext *context, Point c, float r, float glow)
{
    Rect knob;
    Rect shadow;
    Rect glowRect;
    Rect inner;
    GradientStop stops[2];
    Point from;
    Point to;

    if (glow > 0.01f) {
        glowRect = MakeRect(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
        RenderContext_DrawShadow(context, &glowRect, 7.0f, WithAlpha(THEME_GOLD, 0.45f * glow));
    }
    shadow = MakeRect(c.x - r + 2.0f, c.y - r + 4.0f, r * 2.0f - 4.0f, r * 2.0f - 4.0f);
    RenderContext_DrawShadow(context, &shadow, 3.5f, WithAlpha(kBlack, 0.55f));

    knob = MakeRect(c.x - r, c.y - r, r * 2.0f, r * 2.0f);
    stops[0].position = 0.0f;
    stops[0].color = LerpColor(THEME_IVORY, kWhite, 0.3f * glow);
    stops[1].position = 1.0f;
    stops[1].color = THEME_GOLD_LIGHT;
    from.x = c.x;
    from.y = c.y - r;
    to.x = c.x;
    to.y = c.y + r;
    inner = Inset(&knob, 0.5f);
    RenderContext_FillEllipseBrush(context, &knob,
                                   RenderContext_Linear(context, from, to, stops, 2));
    RenderContext_StrokeEllipse(context, &inner, WithAlpha(THEME_GOLD_DEEP, 0.9f), 1.0f);
}

/* ---- TextField -------------------------------------------------------- */

void TextField_Destroy(TextField *field)
{
    PDK_RELEASE(field->layout);
    WStr_Free(&field->text);
    WStr_Free(&field->composition);
}

void TextField_SetUtf8(TextField *field, const char *text)
{
    WStr_Free(&field->text);
    WideFromUtf8(text, &field->text);
    field->caret = field->text.len;
    field->anchor = field->text.len;
    WStr_Clear(&field->composition);
    field->layoutDirty = true;
}

void TextField_Utf8(const TextField *field, Str *out)
{
    Utf8FromWide(WStr_CStr(&field->text), out);
}

int TextField_Utf8To(const TextField *field, char *out, int cap)
{
    Str utf8;
    int written;

    if (out == NULL || cap <= 0) {
        return 0;
    }
    TextField_Utf8(field, &utf8);
    Str_CopyTo(out, cap, Str_CStr(&utf8));
    written = utf8.len < cap ? utf8.len : cap - 1;
    Str_Free(&utf8);
    return written;
}

void TextField_SetFocused(TextField *field, bool focused)
{
    if (field->focused == focused) {
        return;
    }
    field->focused = focused;
    field->blink = 0.0f;
    if (!focused) {
        WStr_Clear(&field->composition);
        field->anchor = field->caret;
        field->dragging = false;
        field->layoutDirty = true;
    }
}

void TextField_Update(TextField *field, float dt)
{
    field->focusT = Approach(field->focusT, field->focused ? 1.0f : 0.0f, 14.0f, dt);
    field->hoverT = Approach(field->hoverT, field->hover ? 1.0f : 0.0f, 16.0f, dt);
    field->blink += dt;
}

void TextField_UpdateHover(TextField *field, float x, float y)
{
    field->hover = Rect_Contains(&field->rect, x, y);
}

static Point TextField_TextOrigin(const TextField *field)
{
    Point origin;

    origin.x = field->rect.x + FieldPadX();
    origin.y = field->rect.y + (field->rect.height - field->lineHeight) * 0.5f;
    return origin;
}

static bool TextField_HasSelection(const TextField *field)
{
    return field->caret != field->anchor;
}

void TextField_Draw(TextField *field, RenderContext *context)
{
    const float radius = 12.0f;
    WStr display;
    bool composing;
    Point origin;
    Rect clip;
    Rect fieldInner;
    GradientStop fieldStops[2];
    Point fieldFrom;
    Point fieldTo;
    TextStyle placeholderStyle;
    Rect placeholderRect;

    if (field->focusT > 0.01f) {
        RenderContext_DrawShadow(context, &field->rect, 9.0f,
                                 WithAlpha(THEME_GOLD, 0.28f * field->focusT));
    }
    fieldStops[0].position = 0.0f;
    fieldStops[0].color = WithAlpha(THEME_ROOM_DEEP, 0.92f);
    fieldStops[1].position = 1.0f;
    fieldStops[1].color =
        WithAlpha(LerpColor(THEME_INK, THEME_INK_RAISED, field->hoverT), 0.92f);
    fieldFrom.x = field->rect.x;
    fieldFrom.y = field->rect.y;
    fieldTo.x = field->rect.x;
    fieldTo.y = field->rect.y + field->rect.height;
    RenderContext_FillRoundedRectBrush(context, &field->rect, radius,
                                       RenderContext_Linear(context, fieldFrom, fieldTo,
                                                            fieldStops, 2));
    fieldInner = Inset(&field->rect, 0.5f);
    RenderContext_StrokeRoundedRect(
        context, &fieldInner, radius,
        WithAlpha(LerpColor(THEME_GOLD, THEME_GOLD_LIGHT, field->focusT),
                  0.26f + 0.16f * field->hoverT + 0.5f * field->focusT),
        1.0f + 0.4f * field->focusT);

    /* display = text with composition spliced in at the caret. */
    WStr_Init(&display);
    composing = field->composition.len > 0;
    if (composing) {
        WStr_Reserve(&display, field->text.len + field->composition.len);
        WStr_AssignN(&display, field->text.data, field->caret);
        WStr_AppendN(&display, field->composition.data, field->composition.len);
        WStr_AppendN(&display, field->text.data + field->caret, field->text.len - field->caret);
    } else {
        WStr_AssignN(&display, WStr_CStr(&field->text), field->text.len);
    }

    if (field->layoutDirty || field->layout == NULL) {
        TextStyle layoutStyle = FieldStyle();

        PDK_RELEASE(field->layout);
        field->layout = RenderContext_CreateTextLayout(context, WStr_CStr(&display), display.len,
                                                       &layoutStyle, 4096.0f,
                                                       field->rect.height);
        field->layoutDirty = false;
        if (field->layout != NULL) {
            DWRITE_TEXT_METRICS metrics;

            if (composing) {
                DWRITE_TEXT_RANGE range;

                range.startPosition = (UINT32)field->caret;
                range.length = (UINT32)field->composition.len;
                PDK_CALL(field->layout, SetUnderline, TRUE, range);
            }
            memset(&metrics, 0, sizeof(metrics));
            PDK_CALL(field->layout, GetMetrics, &metrics);
            if (metrics.height > 1.0f) {
                field->lineHeight = metrics.height;
            }
        }
    }

    origin = TextField_TextOrigin(field);
    clip = Inset(&field->rect, 4.0f);
    RenderContext_PushClip(context, &clip);
    placeholderStyle = FieldStyle();
    placeholderStyle.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    placeholderRect = MakeRect(origin.x, field->rect.y,
                               field->rect.width - FieldPadX() * 2.0f, field->rect.height);
    if (display.len == 0 && field->placeholder[0] != '\0') {
        RenderContext_DrawTextUtf8(context, field->placeholder, &placeholderRect,
                                   &placeholderStyle, THEME_FAINT);
    }
    if (field->layout != NULL) {
        if (TextField_HasSelection(field) && !composing) {
            const int from = field->caret < field->anchor ? field->caret : field->anchor;
            const int length = field->caret > field->anchor ? field->caret - field->anchor
                                                            : field->anchor - field->caret;
            DWRITE_HIT_TEST_METRICS ranges[8];
            UINT32 count = 0;

            memset(ranges, 0, sizeof(ranges));
            if (SUCCEEDED(PDK_CALL(field->layout, HitTestTextRange, (UINT32)from, (UINT32)length,
                                   0.0f, 0.0f, ranges, 8, &count))) {
                UINT32 i;

                for (i = 0; i < count; ++i) {
                    Rect range = MakeRect(origin.x + ranges[i].left, origin.y + ranges[i].top,
                                          ranges[i].width, ranges[i].height);

                    RenderContext_FillRoundedRect(context, &range, 3.0f,
                                                  WithAlpha(THEME_GOLD, 0.30f));
                }
            }
        }
        RenderContext_DrawTextLayout(context, field->layout, origin, THEME_IVORY);

        {
            const int displayCaret = field->caret + (composing ? field->compositionCursor : 0);
            FLOAT px = 0.0f;
            FLOAT py = 0.0f;
            DWRITE_HIT_TEST_METRICS metrics;
            float height;

            memset(&metrics, 0, sizeof(metrics));
            PDK_CALL(field->layout, HitTestTextPosition, (UINT32)displayCaret, FALSE, &px, &py,
                     &metrics);
            height = metrics.height > 1.0f ? metrics.height : field->lineHeight;
            field->caretRect = MakeRect(origin.x + px, origin.y + py, 1.6f, height);
            if (field->focused &&
                fmodf(field->blink, kCaretBlinkPeriod) < kCaretBlinkPeriod * 0.58f) {
                Rect bar = MakeRect(field->caretRect.x - 0.3f, field->caretRect.y + 2.0f,
                                    field->caretRect.width, field->caretRect.height - 4.0f);

                RenderContext_FillRect(context, &bar, THEME_GOLD_LIGHT);
            }
        }
    }
    RenderContext_PopClip(context);
    WStr_Free(&display);
}

static int TextField_ClampToBoundary(const TextField *field, int index)
{
    if (index > 0 && index < field->text.len && IS_LOW_SURROGATE(field->text.data[index])) {
        return index - 1;
    }
    return index;
}

static int TextField_HitIndex(const TextField *field, float x, float y)
{
    Point origin;
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics;
    int index;

    if (field->layout == NULL) {
        return field->text.len;
    }
    origin = TextField_TextOrigin(field);
    memset(&metrics, 0, sizeof(metrics));
    PDK_CALL(field->layout, HitTestPoint, x - origin.x, y - origin.y, &trailing, &inside,
             &metrics);
    index = (int)metrics.textPosition + (trailing ? (int)metrics.length : 0);
    return TextField_ClampToBoundary(field, PDK_CLAMP(index, 0, field->text.len));
}

static int TextField_PrevBoundary(const TextField *field, int index)
{
    if (index <= 0) {
        return 0;
    }
    --index;
    if (index > 0 && IS_LOW_SURROGATE(field->text.data[index]) &&
        IS_HIGH_SURROGATE(field->text.data[index - 1])) {
        --index;
    }
    return index;
}

static int TextField_NextBoundary(const TextField *field, int index)
{
    const int size = field->text.len;

    if (index >= size) {
        return size;
    }
    ++index;
    if (index < size && IS_LOW_SURROGATE(field->text.data[index])) {
        ++index;
    }
    return index;
}

static int TextField_CharCount(const WStr *text)
{
    int count = 0;
    int i;

    for (i = 0; i < text->len; ++i) {
        if (!IS_LOW_SURROGATE(text->data[i])) {
            ++count;
        }
    }
    return count;
}

static void TextField_MoveCaret(TextField *field, int index, bool extend)
{
    field->caret = PDK_CLAMP(index, 0, field->text.len);
    if (!extend) {
        field->anchor = field->caret;
    }
    field->blink = 0.0f;
}

static void TextField_DeleteSelection(TextField *field)
{
    int from;
    int count;
    int i;

    if (!TextField_HasSelection(field)) {
        return;
    }
    from = field->caret < field->anchor ? field->caret : field->anchor;
    count = field->caret > field->anchor ? field->caret - field->anchor
                                         : field->anchor - field->caret;
    for (i = from; i + count <= field->text.len; ++i) {
        field->text.data[i] = field->text.data[i + count];
    }
    field->text.len -= count;
    field->caret = from;
    field->anchor = from;
    field->layoutDirty = true;
}

bool TextField_OnMouseDown(TextField *field, float x, float y, bool shift)
{
    if (!Rect_Contains(&field->rect, x, y)) {
        TextField_SetFocused(field, false);
        return false;
    }
    TextField_SetFocused(field, true);
    if (field->composition.len == 0) {
        TextField_MoveCaret(field, TextField_HitIndex(field, x, y), shift);
        field->dragging = true;
    }
    return true;
}

void TextField_OnMouseMove(TextField *field, float x, float y)
{
    if (field->dragging && field->composition.len == 0) {
        TextField_MoveCaret(field, TextField_HitIndex(field, x, y), true);
    }
}

void TextField_OnMouseUp(TextField *field)
{
    field->dragging = false;
}

bool TextField_OnKeyDown(TextField *field, const KeyEvent *key)
{
    int size;
    int from;
    WStr selected;

    if (!field->focused || field->composition.len > 0) {
        return false;
    }
    size = field->text.len;
    switch (key->key) {
    case VK_LEFT:
        TextField_MoveCaret(field,
                            TextField_HasSelection(field) && !key->shift
                                ? (field->caret < field->anchor ? field->caret : field->anchor)
                                : TextField_PrevBoundary(field, field->caret),
                            key->shift);
        return true;
    case VK_RIGHT:
        TextField_MoveCaret(field,
                            TextField_HasSelection(field) && !key->shift
                                ? (field->caret > field->anchor ? field->caret : field->anchor)
                                : TextField_NextBoundary(field, field->caret),
                            key->shift);
        return true;
    case VK_HOME:
        TextField_MoveCaret(field, 0, key->shift);
        return true;
    case VK_END:
        TextField_MoveCaret(field, size, key->shift);
        return true;
    case VK_BACK:
        if (!TextField_HasSelection(field) && field->caret > 0) {
            field->anchor = TextField_PrevBoundary(field, field->caret);
        }
        TextField_DeleteSelection(field);
        field->blink = 0.0f;
        return true;
    case VK_DELETE:
        if (!TextField_HasSelection(field) && field->caret < size) {
            field->anchor = TextField_NextBoundary(field, field->caret);
        }
        TextField_DeleteSelection(field);
        field->blink = 0.0f;
        return true;
    default:
        break;
    }
    if (!key->ctrl) {
        return false;
    }
    from = field->caret < field->anchor ? field->caret : field->anchor;
    WStr_Init(&selected);
    WStr_AssignN(&selected, field->text.data + from,
                 field->caret > field->anchor ? field->caret - field->anchor
                                              : field->anchor - field->caret);
    switch (key->key) {
    case 'A':
        field->anchor = 0;
        field->caret = size;
        WStr_Free(&selected);
        return true;
    case 'C':
        CopyToClipboard(field->clipboardOwner, &selected);
        WStr_Free(&selected);
        return true;
    case 'X':
        CopyToClipboard(field->clipboardOwner, &selected);
        TextField_DeleteSelection(field);
        WStr_Free(&selected);
        return true;
    case 'V': {
        WStr clip;

        if (ReadClipboard(field->clipboardOwner, &clip)) {
            TextField_Insert(field, &clip);
        }
        WStr_Free(&clip);
        WStr_Free(&selected);
        return true;
    }
    default:
        WStr_Free(&selected);
        return false;
    }
}

/* How many characters `text[i..]` would contribute, applying the Insert filters: control
 * characters are dropped, a surrogate pair counts once, a lone surrogate is dropped. */
static int TextField_AcceptableCount(const WStr *text, int from)
{
    int count = 0;
    int i;

    for (i = from; i < text->len; ++i) {
        const wchar_t c = text->data[i];

        if (c < 0x20 || c == 0x7F) {
            continue;
        }
        if (IS_HIGH_SURROGATE(c)) {
            if (i + 1 < text->len && IS_LOW_SURROGATE(text->data[i + 1])) {
                ++count;
                ++i;
            }
            continue;
        }
        if (IS_LOW_SURROGATE(c)) {
            continue;
        }
        ++count;
    }
    return count;
}

/* Writes at most `limit` accepted *characters* of `text` at `dst`, advancing `i` over the
 * surrogate pairs it consumed (a pair is one character and two units).  Returns the number of
 * wchar_t units written. */
static int TextField_WriteAccepted(const WStr *text, int *i, int limit, wchar_t *dst)
{
    int written = 0;
    int characters = 0;

    while (*i < text->len && characters < limit) {
        const wchar_t c = text->data[*i];

        if (c < 0x20 || c == 0x7F) {
            ++*i;
            continue;
        }
        if (IS_HIGH_SURROGATE(c)) {
            if (*i + 1 < text->len && IS_LOW_SURROGATE(text->data[*i + 1])) {
                dst[written] = c;
                ++written;
                ++*i;
                dst[written] = text->data[*i];
                ++written;
                ++characters;
            }
            ++*i;
            continue;
        }
        if (IS_LOW_SURROGATE(c)) {
            ++*i;
            continue;
        }
        dst[written] = c;
        ++written;
        ++characters;
        ++*i;
    }
    return written;
}

/*
 * The old code built `accepted` in a loop (surrogate pairs counted once against maxChars,
 * control characters dropped), then inserted it: insert() moves the tail right first and writes
 * the run at the caret.  Here the same two phases run against the WStr buffer directly: reserve
 * the worst case once, shift the tail up by exactly the accepted unit count, then write the run
 * at the caret.  The reserve also guarantees the trailing NUL slot.
 */
void TextField_Insert(TextField *field, const WStr *text)
{
    const int capacity = field->maxChars - TextField_CharCount(&field->text);
    int accepted;
    int units;
    int cursor = 0;
    int k;

    if (!field->focused) {
        return;
    }
    TextField_DeleteSelection(field);
    if (capacity <= 0 || !WStr_Reserve(&field->text, text->len)) {
        return;
    }
    accepted = TextField_AcceptableCount(text, 0);
    if (accepted > capacity) {
        accepted = capacity;
    }
    /* Three phases against the one buffer: shift the tail to the far right end of the reserved
     * run, write the accepted characters into the gap, then drop the unused gap by shifting the
     * tail back to sit directly after the accepted run.  The reserve above also guarantees the
     * trailing NUL slot. */
    for (k = field->text.len - 1; k >= field->caret; --k) {
        field->text.data[k + text->len] = field->text.data[k];
    }
    units = TextField_WriteAccepted(text, &cursor, accepted, field->text.data + field->caret);
    for (k = 0; k <= field->text.len - field->caret; ++k) {
        field->text.data[field->caret + units + k] =
            field->text.data[field->caret + text->len + k];
    }
    field->text.len += units;
    field->text.data[field->text.len] = L'\0';
    TextField_MoveCaret(field, field->caret + units, false);
    field->layoutDirty = true;
}

void TextField_SetComposition(TextField *field, const WStr *text, int cursor)
{
    if (text->len > 0 && field->composition.len == 0) {
        TextField_DeleteSelection(field);
    }
    WStr_Clear(&field->composition);
    WStr_AppendN(&field->composition, text->data, text->len);
    field->compositionCursor = PDK_CLAMP(cursor, 0, text->len);
    field->blink = 0.0f;
    field->layoutDirty = true;
}

bool TextField_CaretRect(const TextField *field, Rect *out)
{
    *out = field->caretRect;
    return true;
}

/* ---- Slider ----------------------------------------------------------- */

void Slider_Update(Slider *slider, float dt)
{
    slider->hoverT = Approach(slider->hoverT, slider->hover || slider->dragging ? 1.0f : 0.0f,
                              16.0f, dt);
}

void Slider_Draw(const Slider *slider, RenderContext *context)
{
    const float cy = slider->rect.y + slider->rect.height * 0.5f;
    const Rect track = MakeRect(slider->rect.x, cy - 3.0f, slider->rect.width, 6.0f);
    const float kx = slider->rect.x + slider->rect.width * Clamp01(slider->value);

    RenderContext_FillRoundedRect(context, &track, 3.0f, WithAlpha(THEME_ROOM_DEEP, 0.9f));
    RenderContext_StrokeRoundedRect(context, &track, 3.0f, WithAlpha(THEME_GOLD, 0.22f), 1.0f);
    if (kx > slider->rect.x + 1.0f) {
        const Rect fill = MakeRect(slider->rect.x, track.y, kx - slider->rect.x, track.height);
        const float endX = fill.x + MaxF(fill.width, 1.0f);
        GradientStop stops[2];
        Point from;
        Point to;

        stops[0].position = 0.0f;
        stops[0].color = THEME_GOLD_DEEP;
        stops[1].position = 1.0f;
        stops[1].color = THEME_GOLD_LIGHT;
        from.x = fill.x;
        from.y = fill.y;
        to.x = endX;
        to.y = fill.y;
        RenderContext_FillRoundedRectBrush(context, &fill, 3.0f,
                                           RenderContext_Linear(context, from, to, stops, 2));
    }
    {
        int i;

        for (i = 1; i < 4; ++i) {
            const float tx = slider->rect.x + slider->rect.width * (float)i * 0.25f;
            const Point from = {tx, cy + 9.0f};
            const Point to = {tx, cy + 13.0f};

            RenderContext_DrawLine(context, from, to, WithAlpha(THEME_GOLD, 0.28f), 1.0f);
        }
    }
    {
        Point center;

        center.x = kx;
        center.y = cy;
        DrawKnob(context, center, 11.0f + 1.5f * slider->hoverT, slider->hoverT);
    }
}

static bool Slider_SetFromX(Slider *slider, float x)
{
    const float next = Clamp01((x - slider->rect.x) / MaxF(slider->rect.width, 1.0f));
    const float stepped = (float)RoundToInt(next * 100.0f) / 100.0f;

    if (fabsf(stepped - slider->value) < 0.0001f) {
        return false;
    }
    slider->value = stepped;
    return true;
}

bool Slider_OnMouseDown(Slider *slider, float x, float y)
{
    const Rect hit = MakeRect(slider->rect.x - 14.0f, slider->rect.y - 6.0f,
                              slider->rect.width + 28.0f, slider->rect.height + 12.0f);

    if (!Rect_Contains(&hit, x, y)) {
        return false;
    }
    slider->dragging = true;
    Slider_SetFromX(slider, x);
    return true;
}

bool Slider_OnMouseMove(Slider *slider, float x, float y)
{
    const Rect hit = MakeRect(slider->rect.x - 14.0f, slider->rect.y - 6.0f,
                              slider->rect.width + 28.0f, slider->rect.height + 12.0f);

    slider->hover = Rect_Contains(&hit, x, y);
    return slider->dragging && Slider_SetFromX(slider, x);
}

bool Slider_OnMouseUp(Slider *slider)
{
    const bool was = slider->dragging;

    slider->dragging = false;
    return was;
}

/* ---- Segmented -------------------------------------------------------- */

void Segmented_SetOptions(Segmented *segmented, const char *const *options, int count)
{
    int i;

    segmented->optionCount = 0;
    for (i = 0; i < PDK_SEGMENTED_MAX; ++i) {
        segmented->options[i][0] = '\0';
    }
    if (options == NULL || count <= 0) {
        return;
    }
    if (count > PDK_SEGMENTED_MAX) {
        count = PDK_SEGMENTED_MAX;
    }
    for (i = 0; i < count; ++i) {
        Str_CopyTo(segmented->options[i], PDK_SEGMENTED_OPTION_CAP, options[i]);
    }
    segmented->optionCount = count;
}

void Segmented_Update(Segmented *segmented, float dt)
{
    segmented->slide = Approach(segmented->slide, (float)segmented->selected, 18.0f, dt);
}

static int Segmented_HitTest(const Segmented *segmented, float x, float y)
{
    float width;

    if (segmented->optionCount <= 0 || !Rect_Contains(&segmented->rect, x, y)) {
        return -1;
    }
    width = segmented->rect.width / (float)segmented->optionCount;
    return PDK_CLAMP((int)((x - segmented->rect.x) / width), 0, segmented->optionCount - 1);
}

void Segmented_UpdateHover(Segmented *segmented, float x, float y)
{
    segmented->hover = Segmented_HitTest(segmented, x, y);
}

bool Segmented_OnMouseDown(Segmented *segmented, float x, float y)
{
    const int hit = Segmented_HitTest(segmented, x, y);

    if (hit < 0 || hit == segmented->selected) {
        return false;
    }
    segmented->selected = hit;
    return true;
}

void Segmented_Draw(const Segmented *segmented, RenderContext *context)
{
    const float radius = segmented->rect.height * 0.5f;
    float width;
    Rect pill;
    Rect pillShadow;
    Rect inner;
    TextStyle style;
    int i;

    if (segmented->optionCount <= 0) {
        return;
    }
    RenderContext_FillRoundedRect(context, &segmented->rect, radius,
                                  WithAlpha(THEME_ROOM_DEEP, 0.9f));
    inner = Inset(&segmented->rect, 0.5f);
    RenderContext_StrokeRoundedRect(context, &inner, radius, WithAlpha(THEME_GOLD, 0.28f), 1.0f);

    width = segmented->rect.width / (float)segmented->optionCount;
    pill = MakeRect(segmented->rect.x + 3.0f + width * segmented->slide,
                    segmented->rect.y + 3.0f, width - 6.0f, segmented->rect.height - 6.0f);
    pillShadow = MakeRect(pill.x + 2.0f, pill.y + 3.0f, pill.width - 4.0f, pill.height - 2.0f);
    RenderContext_DrawShadow(context, &pillShadow, 4.0f, WithAlpha(kBlack, 0.45f));
    {
        GradientStop stops[3];
        Point from;
        Point to;

        stops[0].position = 0.0f;
        stops[0].color = THEME_GOLD_LIGHT;
        stops[1].position = 0.6f;
        stops[1].color = THEME_GOLD;
        stops[2].position = 1.0f;
        stops[2].color = THEME_GOLD_DEEP;
        from.x = pill.x;
        from.y = pill.y;
        to.x = pill.x;
        to.y = pill.y + pill.height;
        RenderContext_FillRoundedRectBrush(context, &pill, pill.height * 0.5f,
                                           RenderContext_Linear(context, from, to, stops, 3));
    }

    style = TextStyle_Default();
    style.size = 16.0f;
    style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
    style.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    style.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    style.wrap = false;
    for (i = 0; i < segmented->optionCount; ++i) {
        const float weight = Clamp01(1.0f - fabsf(segmented->slide - (float)i));
        const D2D1_COLOR_F idle = i == segmented->hover ? THEME_IVORY : THEME_MUTED;
        const Rect cell = MakeRect(segmented->rect.x + width * (float)i, segmented->rect.y,
                                   width, segmented->rect.height);

        RenderContext_DrawTextUtf8(context, segmented->options[i], &cell, &style,
                                   LerpColor(idle, THEME_GOLD_INK, weight));
    }
}

/* ---- Toggle ----------------------------------------------------------- */

void Toggle_Update(Toggle *toggle, float dt)
{
    toggle->t = Approach(toggle->t, toggle->on ? 1.0f : 0.0f, 16.0f, dt);
    toggle->hoverT = Approach(toggle->hoverT, toggle->hover ? 1.0f : 0.0f, 16.0f, dt);
}

void Toggle_Draw(const Toggle *toggle, RenderContext *context)
{
    const float radius = toggle->rect.height * 0.5f;

    RenderContext_FillRoundedRect(context, &toggle->rect, radius,
                                  WithAlpha(THEME_ROOM_DEEP, 0.9f));
    if (toggle->t > 0.01f) {
        GradientStop stops[2];
        Point from;
        Point to;

        stops[0].position = 0.0f;
        stops[0].color = THEME_GOLD;
        stops[1].position = 1.0f;
        stops[1].color = THEME_GOLD_DEEP;
        from.x = toggle->rect.x;
        from.y = toggle->rect.y;
        to.x = toggle->rect.x;
        to.y = toggle->rect.y + toggle->rect.height;
        RenderContext_PushOpacity(context, toggle->t);
        RenderContext_FillRoundedRectBrush(context, &toggle->rect, radius,
                                           RenderContext_Linear(context, from, to, stops, 2));
        RenderContext_PopOpacity(context);
    }
    {
        Rect inner = Inset(&toggle->rect, 0.5f);

        RenderContext_StrokeRoundedRect(context, &inner, radius,
                                        WithAlpha(THEME_GOLD, 0.3f + 0.3f * toggle->hoverT), 1.0f);
    }
    {
        const float r = radius - 4.0f;
        const float cx = Lerp(toggle->rect.x + radius,
                              toggle->rect.x + toggle->rect.width - radius,
                              EaseInOutSine(toggle->t));
        Point center;

        center.x = cx;
        center.y = toggle->rect.y + radius;
        DrawKnob(context, center, r, toggle->hoverT * 0.6f);
    }
}

void Toggle_UpdateHover(Toggle *toggle, float x, float y)
{
    toggle->hover = Rect_Contains(&toggle->rect, x, y);
}

bool Toggle_OnMouseDown(Toggle *toggle, float x, float y)
{
    if (!Rect_Contains(&toggle->rect, x, y)) {
        return false;
    }
    toggle->on = !toggle->on;
    return true;
}
