#include "graphics/win_compat.h"

#include "ui/Widgets.h"

#include "graphics/d2d_c.h"

#include <math.h>

static const D2D1_COLOR_F kBlack = {0.0f, 0.0f, 0.0f, 1.0f};
static const D2D1_COLOR_F kWhite = {1.0f, 1.0f, 1.0f, 1.0f};

static float MinF(float a, float b)
{
    return a < b ? a : b;
}

static float MaxF(float a, float b)
{
    return a > b ? a : b;
}

static Point MakePoint(float x, float y)
{
    Point p;

    p.x = x;
    p.y = y;
    return p;
}

static Rect MakeRect(float x, float y, float width, float height)
{
    Rect r;

    r.x = x;
    r.y = y;
    r.width = width;
    r.height = height;
    return r;
}

static GradientStop Stop(float position, D2D1_COLOR_F color)
{
    GradientStop stop;

    stop.position = position;
    stop.color = color;
    return stop;
}

static Point Center(const Rect *rect)
{
    return MakePoint(rect->x + rect->width * 0.5f, rect->y + rect->height * 0.5f);
}

static Rect Offset(const Rect *rect, float dx, float dy)
{
    return MakeRect(rect->x + dx, rect->y + dy, rect->width, rect->height);
}

static Rect Inset(const Rect *rect, float d)
{
    return MakeRect(rect->x + d, rect->y + d, rect->width - d * 2.0f, rect->height - d * 2.0f);
}

/* Centered semi-bold at `size`, with wrapping off -- the shape DrawButtonLabel and the chips use. */
static TextStyle LabelStyle(float size, int weight)
{
    TextStyle style = TextStyle_Default();

    style.size = size;
    style.weight = weight;
    style.align = DWRITE_TEXT_ALIGNMENT_CENTER;
    style.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
    style.wrap = false;
    return style;
}

static void DrawButtonLabel(RenderContext *context, const Button *button, D2D1_COLOR_F color)
{
    TextStyle style = LabelStyle(button->fontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD);
    float iconSize;
    float textWidth;
    float gap;
    float total;
    float left;

    if (button->icon == UI_ICON_NONE) {
        RenderContext_DrawTextUtf8(context, button->text, &button->rect, &style, color);
        return;
    }
    iconSize = MinF(button->rect.height * 0.46f, 22.0f);
    if (button->text[0] == '\0') {
        const Point c = Center(&button->rect);
        const Rect box = MakeRect(c.x - iconSize * 0.5f, c.y - iconSize * 0.5f, iconSize, iconSize);

        Icons_Draw(context, button->icon, &box, color);
        return;
    }
    {
        Size measured;
        Rect box;
        Rect textRect;

        RenderContext_MeasureText(context, button->text, &style, 4096.0f, &measured);
        textWidth = measured.width;
        gap = 7.0f;
        total = iconSize + gap + textWidth;
        left = button->rect.x + (button->rect.width - total) * 0.5f;
        box = MakeRect(left, button->rect.y + (button->rect.height - iconSize) * 0.5f, iconSize,
                       iconSize);
        Icons_Draw(context, button->icon, &box, color);

        style.align = DWRITE_TEXT_ALIGNMENT_LEADING;
        textRect = MakeRect(left + iconSize + gap, button->rect.y, textWidth + 4.0f,
                            button->rect.height);
        RenderContext_DrawTextUtf8(context, button->text, &textRect, &style, color);
    }
}

void Button_Update(Button *button, float dt)
{
    button->hoverT = Approach(button->hoverT,
                              button->hover && button->enabled && button->visible ? 1.0f : 0.0f,
                              16.0f, dt);
    button->pressT = Approach(button->pressT, 0.0f, 12.0f, dt);
    button->visibleT = Approach(button->visibleT, button->visible ? 1.0f : 0.0f, 14.0f, dt);
}

void Button_Draw(const Button *button, RenderContext *context)
{
    const float h = button->enabled ? button->hoverT : 0.0f;
    const Point center = Center(&button->rect);
    const float radius = button->rect.height * 0.5f;
    GradientStop stops[3];

    if (!button->visible && button->visibleT < 0.02f) {
        return;
    }

    RenderContext_PushOpacity(context,
                              (button->enabled ? 1.0f : 0.42f) *
                                  (button->visible ? MaxF(button->visibleT, 0.02f)
                                                   : button->visibleT));
    RenderContext_PushScale(context, 1.0f + 0.018f * h - 0.04f * button->pressT, center);
    RenderContext_PushTranslation(context, 0.0f, -1.5f * h);

    switch (button->style) {
    case UI_BUTTON_PRIMARY: {
        D2D1_COLOR_F top;

        {
            Rect inner = Inset(&button->rect, 3.0f);
            Rect shadowRect = Offset(&inner, 0.0f, 5.0f);

            RenderContext_DrawShadow(context, &shadowRect, 7.0f, WithAlpha(kBlack, 0.55f));
        }
        if (h > 0.01f) {
            RenderContext_DrawShadow(context, &button->rect, 10.0f,
                                     WithAlpha(THEME_GOLD, 0.40f * h));
        }
        top = LerpColor(THEME_GOLD_LIGHT, kWhite, 0.25f * h);
        {
            const Point from = MakePoint(button->rect.x, button->rect.y);
            const Point to = MakePoint(button->rect.x, button->rect.y + button->rect.height);

            stops[0] = Stop(0.0f, top);
            stops[1] = Stop(0.55f, LerpColor(THEME_GOLD, THEME_GOLD_LIGHT, 0.3f * h));
            stops[2] = Stop(1.0f, THEME_GOLD_DEEP);
            RenderContext_FillRoundedRectBrush(context, &button->rect, radius,
                                               RenderContext_Linear(context, from, to, stops, 3));
        }
        {
            const Rect inner = Inset(&button->rect, 0.5f);

            RenderContext_StrokeRoundedRect(context, &inner, radius,
                                            WithAlpha(THEME_GOLD_DEEP, 0.9f), 1.0f);
        }
        RenderContext_DrawLine(context, MakePoint(button->rect.x + radius, button->rect.y + 1.6f),
                               MakePoint(button->rect.x + button->rect.width - radius,
                                         button->rect.y + 1.6f),
                               WithAlpha(kWhite, 0.45f), 1.0f);
        DrawButtonLabel(context, button, THEME_GOLD_INK);
        break;
    }
    case UI_BUTTON_SECONDARY: {
        Rect inner;

        inner = Inset(&button->rect, 3.0f);
        {
            const Rect shadowRect = Offset(&inner, 0.0f, 4.0f);

            RenderContext_DrawShadow(context, &shadowRect, 6.0f, WithAlpha(kBlack, 0.45f));
        }
        {
            const Point from = MakePoint(button->rect.x, button->rect.y);
            const Point to = MakePoint(button->rect.x, button->rect.y + button->rect.height);

            stops[0] = Stop(0.0f, WithAlpha(LerpColor(THEME_INK_RAISED, THEME_INK_HOVER, h), 0.96f));
            stops[1] = Stop(1.0f, WithAlpha(THEME_INK, 0.96f));
            RenderContext_FillRoundedRectBrush(context, &button->rect, radius,
                                               RenderContext_Linear(context, from, to, stops, 2));
        }
        inner = Inset(&button->rect, 0.5f);
        RenderContext_StrokeRoundedRect(context, &inner, radius,
                                        WithAlpha(THEME_GOLD, 0.36f + 0.5f * h), 1.0f);
        DrawButtonLabel(context, button, LerpColor(THEME_IVORY, THEME_GOLD_LIGHT, h));
        break;
    }
    case UI_BUTTON_GHOST: {
        const Rect inner = Inset(&button->rect, 0.5f);

        RenderContext_FillRoundedRect(context, &button->rect, radius,
                                      WithAlpha(kWhite, 0.03f + 0.06f * h));
        RenderContext_StrokeRoundedRect(context, &inner, radius,
                                        WithAlpha(THEME_IVORY, 0.22f + 0.3f * h), 1.0f);
        DrawButtonLabel(context, button, LerpColor(THEME_MUTED, THEME_IVORY, h));
        break;
    }
    case UI_BUTTON_DANGER: {
        Rect inner = Inset(&button->rect, 3.0f);

        {
            const Rect shadowRect = Offset(&inner, 0.0f, 5.0f);

            RenderContext_DrawShadow(context, &shadowRect, 7.0f, WithAlpha(kBlack, 0.5f));
        }
        if (h > 0.01f) {
            RenderContext_DrawShadow(context, &button->rect, 10.0f,
                                     WithAlpha(THEME_CINNABAR, 0.45f * h));
        }
        {
            const Point from = MakePoint(button->rect.x, button->rect.y);
            const Point to = MakePoint(button->rect.x, button->rect.y + button->rect.height);

            stops[0] = Stop(0.0f, LerpColor(THEME_CINNABAR_LIGHT, kWhite, 0.12f * h));
            stops[1] = Stop(0.6f, THEME_CINNABAR);
            stops[2] = Stop(1.0f, THEME_CINNABAR_DEEP);
            RenderContext_FillRoundedRectBrush(context, &button->rect, radius,
                                               RenderContext_Linear(context, from, to, stops, 3));
        }
        inner = Inset(&button->rect, 0.5f);
        RenderContext_StrokeRoundedRect(context, &inner, radius,
                                        WithAlpha(THEME_CINNABAR_DEEP, 0.9f), 1.0f);
        RenderContext_DrawLine(context, MakePoint(button->rect.x + radius, button->rect.y + 1.6f),
                               MakePoint(button->rect.x + button->rect.width - radius,
                                         button->rect.y + 1.6f),
                               WithAlpha(kWhite, 0.28f), 1.0f);
        DrawButtonLabel(context, button, THEME_IVORY);
        break;
    }
    case UI_BUTTON_ICON: {
        Rect inner = Inset(&button->rect, 4.0f);

        {
            const Rect shadowRect = Offset(&inner, 0.0f, 3.0f);

            RenderContext_DrawShadow(context, &shadowRect, 5.0f, WithAlpha(kBlack, 0.5f));
        }
        {
            const Point from = MakePoint(button->rect.x, button->rect.y);
            const Point to = MakePoint(button->rect.x, button->rect.y + button->rect.height);

            stops[0] = Stop(0.0f, LerpColor(THEME_INK_RAISED, THEME_INK_HOVER, h));
            stops[1] = Stop(1.0f, THEME_INK);
            RenderContext_FillEllipseBrush(context, &button->rect,
                                           RenderContext_Linear(context, from, to, stops, 2));
        }
        inner = Inset(&button->rect, 0.5f);
        RenderContext_StrokeEllipse(context, &inner, WithAlpha(THEME_GOLD, 0.40f + 0.5f * h), 1.0f);
        DrawButtonLabel(context, button, LerpColor(THEME_IVORY, THEME_GOLD_LIGHT, h));
        break;
    }
    default:
        break;
    }

    RenderContext_PopTransform(context);
    RenderContext_PopTransform(context);
    RenderContext_PopOpacity(context);
}

bool Button_HitTest(const Button *button, float x, float y)
{
    if (!button->visible || !button->enabled) {
        return false;
    }
    if (button->style == UI_BUTTON_ICON) {
        const Point c = Center(&button->rect);
        const float r = MinF(button->rect.width, button->rect.height) * 0.5f;
        const float dx = x - c.x;
        const float dy = y - c.y;

        return dx * dx + dy * dy <= r * r;
    }
    return Rect_Contains(&button->rect, x, y);
}

void Button_UpdateHover(Button *button, float x, float y)
{
    button->hover = Button_HitTest(button, x, y);
}

void ButtonGroup_DrawAll(RenderContext *context, const Button *buttons, int count)
{
    for (int i = 0; i < count; ++i) {
        Button_Draw(&buttons[i], context);
    }
}

void ButtonGroup_UpdateAll(Button *buttons, int count, float dt)
{
    for (int i = 0; i < count; ++i) {
        Button_Update(&buttons[i], dt);
    }
}

int ButtonGroup_Hit(Button *buttons, int count, float x, float y)
{
    for (int i = 0; i < count; ++i) {
        if (Button_HitTest(&buttons[i], x, y)) {
            buttons[i].pressT = 1.0f;
            return i;
        }
    }
    return -1;
}

void ButtonGroup_UpdateHover(Button *buttons, int count, float x, float y)
{
    for (int i = 0; i < count; ++i) {
        Button_UpdateHover(&buttons[i], x, y);
    }
}

void Widgets_DrawPanel(RenderContext *context, const Rect *rect, const PanelStyle *style)
{
    /* The C API cannot carry the C++ default argument `style = {}`, so NULL means the defaults --
     * exactly what the facade's default-constructed PanelStyle produced.  Without this, a caller
     * that writes NULL (which reads like "no style") takes the whole process down on the first
     * dereference. */
    const PanelStyle defaults = PanelStyle_Default();

    if (style == NULL) {
        style = &defaults;
    }
    if (style->shadow > 0.0f) {
        Rect inner = Inset(rect, 6.0f);
        const Rect shadowRect = Offset(&inner, 0.0f, 12.0f);

        RenderContext_DrawShadow(context, &shadowRect, 18.0f,
                                 WithAlpha(kBlack, 0.55f * style->shadow));
    }
    {
        const Point from = MakePoint(rect->x, rect->y);
        const Point to = MakePoint(rect->x, rect->y + rect->height);
        GradientStop stops[2];

        stops[0] = Stop(0.0f, WithAlpha(style->top, style->fillAlpha));
        stops[1] = Stop(1.0f, WithAlpha(style->bottom, style->fillAlpha));
        RenderContext_FillRoundedRectBrush(context, rect, style->radius,
                                           RenderContext_Linear(context, from, to, stops, 2));
    }
    {
        const Point from = MakePoint(rect->x, rect->y);
        const Point to = MakePoint(rect->x, rect->y + rect->height);
        const Rect inner = Inset(rect, 0.5f);
        GradientStop stops[3];

        stops[0] = Stop(0.0f, WithAlpha(THEME_GOLD, 0.62f));
        stops[1] = Stop(0.5f, WithAlpha(THEME_GOLD, 0.28f));
        stops[2] = Stop(1.0f, WithAlpha(THEME_GOLD, 0.44f));
        RenderContext_StrokeRoundedRectBrush(context, &inner, style->radius,
                                             RenderContext_Linear(context, from, to, stops, 3),
                                             1.0f);
    }
    if (style->innerLine) {
        const Rect inner = Inset(rect, 5.5f);

        RenderContext_StrokeRoundedRect(context, &inner, MaxF(2.0f, style->radius - 5.0f),
                                        WithAlpha(THEME_GOLD, 0.10f), 1.0f);
    }
    if (style->ornament) {
        Widgets_DrawOrnamentCorners(context, rect, WithAlpha(THEME_GOLD, 0.55f), 16.0f, 9.0f);
    }
}

void Widgets_DrawOrnamentCorners(RenderContext *context, const Rect *rect, D2D1_COLOR_F color,
                                 float size, float inset)
{
    /* A small 回-fret hook mirrored into each corner. */
    Point outer[3];
    Point hook[4];
    float corners[4][4];

    outer[0] = MakePoint(0.0f, size);
    outer[1] = MakePoint(0.0f, 0.0f);
    outer[2] = MakePoint(size, 0.0f);
    hook[0] = MakePoint(4.5f, size - 5.0f);
    hook[1] = MakePoint(4.5f, 4.5f);
    hook[2] = MakePoint(size - 5.0f, 4.5f);
    hook[3] = MakePoint(size - 5.0f, 9.0f);

    corners[0][0] = rect->x + inset;
    corners[0][1] = rect->y + inset;
    corners[0][2] = 1.0f;
    corners[0][3] = 1.0f;
    corners[1][0] = rect->x + rect->width - inset;
    corners[1][1] = rect->y + inset;
    corners[1][2] = -1.0f;
    corners[1][3] = 1.0f;
    corners[2][0] = rect->x + inset;
    corners[2][1] = rect->y + rect->height - inset;
    corners[2][2] = 1.0f;
    corners[2][3] = -1.0f;
    corners[3][0] = rect->x + rect->width - inset;
    corners[3][1] = rect->y + rect->height - inset;
    corners[3][2] = -1.0f;
    corners[3][3] = -1.0f;

    for (int c = 0; c < 4; ++c) {
        Point a[3];
        Point b[4];

        for (int i = 0; i < 3; ++i) {
            a[i] = MakePoint(corners[c][0] + outer[i].x * corners[c][2],
                             corners[c][1] + outer[i].y * corners[c][3]);
        }
        for (int i = 0; i < 4; ++i) {
            b[i] = MakePoint(corners[c][0] + hook[i].x * corners[c][2],
                             corners[c][1] + hook[i].y * corners[c][3]);
        }
        RenderContext_StrokePolyline(context, a, 3, color, 1.2f, false);
        RenderContext_StrokePolyline(context, b, 4, WithAlpha(color, 0.7f), 1.0f, false);
    }
}

void Widgets_DrawHairline(RenderContext *context, float x0, float x1, float y, float alpha)
{
    const Point from = MakePoint(x0, y);
    const Point to = MakePoint(x1, y);
    GradientStop stops[3];

    stops[0] = Stop(0.0f, WithAlpha(THEME_GOLD, 0.0f));
    stops[1] = Stop(0.5f, WithAlpha(THEME_GOLD, alpha));
    stops[2] = Stop(1.0f, WithAlpha(THEME_GOLD, 0.0f));
    RenderContext_DrawLineBrush(context, from, to,
                                RenderContext_Linear(context, from, to, stops, 3), 1.0f);
}

Rect Widgets_ChipRect(RenderContext *context, Point anchor, Anchor align, const char *text,
                      const ChipStyle *style)
{
    TextStyle textStyle = LabelStyle(style->fontSize, style->weight);
    Size measured;
    float width;
    float x = anchor.x;

    RenderContext_MeasureText(context, text, &textStyle, 4096.0f, &measured);
    width = measured.width + style->padX * 2.0f;
    if (align == UI_ANCHOR_CENTER) {
        x -= width * 0.5f;
    } else if (align == UI_ANCHOR_RIGHT) {
        x -= width;
    }
    return MakeRect(x, anchor.y - style->height * 0.5f, width, style->height);
}

void Widgets_DrawChipInRect(RenderContext *context, const Rect *rect, const char *text,
                            const ChipStyle *style)
{
    const float radius = rect->height * 0.5f;
    TextStyle textStyle = LabelStyle(style->fontSize, style->weight);

    RenderContext_FillRoundedRect(context, rect, radius, style->fill);
    if (style->stroke.a > 0.0f) {
        const Rect inner = Inset(rect, 0.5f);

        RenderContext_StrokeRoundedRect(context, &inner, radius, style->stroke, 1.0f);
    }
    RenderContext_DrawTextUtf8(context, text, rect, &textStyle, style->text);
}

Rect Widgets_DrawChip(RenderContext *context, Point anchor, Anchor align, const char *text,
                      const ChipStyle *style)
{
    /* NULL means the defaults, as in Widgets_DrawPanel. */
    const ChipStyle defaults = ChipStyle_Default();
    Rect rect;

    if (style == NULL) {
        style = &defaults;
    }
    rect = Widgets_ChipRect(context, anchor, align, text, style);

    Widgets_DrawChipInRect(context, &rect, text, style);
    return rect;
}

void Widgets_AvatarLabel(const char *name, char *out, int cap)
{
    unsigned char lead;
    int length = 1;
    int taken = 0;

    if (name == NULL || name[0] == '\0') {
        Str_CopyTo(out, cap, "?");
        return;
    }
    lead = (unsigned char)name[0];
    if (lead < 0x80) {
        const int limit = (int)strlen(name) < 3 ? (int)strlen(name) : 3;

        Str_CopyTo(out, cap, name);
        /* Truncate to the ASCII limit Str_CopyTo produced above. */
        if (limit < (int)strlen(out)) {
            out[limit] = '\0';
        }
        return;
    }
    if ((lead & 0xE0u) == 0xC0u) {
        length = 2;
    } else if ((lead & 0xF0u) == 0xE0u) {
        length = 3;
    } else if ((lead & 0xF8u) == 0xF0u) {
        length = 4;
    }
    while (taken < length && name[taken] != '\0' && taken + 1 < cap) {
        out[taken] = name[taken];
        ++taken;
    }
    out[taken] = '\0';
}

void Widgets_DrawRadialGlow(RenderContext *context, Point center, float radius, D2D1_COLOR_F color,
                            float ring)
{
    /* Keep the alpha out of the gradient stops and drive it with opacity: animated alphas would
     * otherwise mint a fresh cached brush every frame. */
    D2D1_COLOR_F solid;
    D2D1_COLOR_F clear;
    Rect box;
    PDK_ID2D1Brush *brush;

    solid = color;
    solid.a = 1.0f;
    clear = color;
    clear.a = 0.0f;

    RenderContext_PushOpacity(context, color.a);
    if (ring > 0.0f) {
        GradientStop stops[4];

        stops[0] = Stop(0.0f, clear);
        stops[1] = Stop(ring * 0.82f, clear);
        stops[2] = Stop(ring, solid);
        stops[3] = Stop(1.0f, clear);
        brush = RenderContext_Radial(context, center, radius, radius, stops, 4);
    } else {
        GradientStop stops[2];

        stops[0] = Stop(0.0f, solid);
        stops[1] = Stop(1.0f, clear);
        brush = RenderContext_Radial(context, center, radius, radius, stops, 2);
    }
    box = MakeRect(center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f);
    RenderContext_FillEllipseBrush(context, &box, brush);
    RenderContext_PopOpacity(context);
}

void Widgets_DrawAvatar(RenderContext *context, const Rect *rect, const char *label, bool active,
                        float time)
{
    const Point c = Center(rect);
    const float r = rect->width * 0.5f;
    bool ascii;
    TextStyle style;
    Rect shadowRect;
    Rect inner;

    if (active) {
        const float pulse = 0.5f + 0.5f * sinf(time * 4.2f);

        Widgets_DrawRadialGlow(context, c, r * (1.55f + 0.12f * pulse),
                               WithAlpha(THEME_GOLD, 0.55f + 0.3f * pulse), 0.68f);
    }
    shadowRect = MakeRect(rect->x + 4.0f, rect->y + 7.0f, rect->width - 8.0f, rect->height - 8.0f);
    RenderContext_DrawShadow(context, &shadowRect, 5.0f, WithAlpha(kBlack, 0.55f));

    {
        const Point focus = MakePoint(c.x - r * 0.35f, c.y - r * 0.45f);
        GradientStop stops[2];

        stops[0] = Stop(0.0f, THEME_INK_HOVER);
        stops[1] = Stop(1.0f, THEME_INK);
        RenderContext_FillEllipseBrush(context, rect,
                                       RenderContext_Radial(context, focus, r * 1.6f, r * 1.6f,
                                                            stops, 2));
    }
    inner = Inset(rect, 0.75f);
    RenderContext_StrokeEllipse(context, &inner,
                                active ? THEME_GOLD_LIGHT : WithAlpha(THEME_GOLD, 0.7f),
                                active ? 2.0f : 1.3f);
    inner = Inset(rect, 4.0f);
    RenderContext_StrokeEllipse(context, &inner, WithAlpha(THEME_GOLD, 0.18f), 1.0f);

    ascii = label[0] != '\0' && (unsigned char)label[0] < 0x80;
    style = ascii ? LabelStyle(r * 0.62f, DWRITE_FONT_WEIGHT_SEMI_BOLD)
                  : LabelStyle(r * 0.95f, DWRITE_FONT_WEIGHT_BOLD);
    if (!ascii) {
        style.family = FONT_FAMILY_KAI;
    }
    RenderContext_DrawTextUtf8(context, label, rect, &style, THEME_GOLD_LIGHT);
}

void Widgets_DrawSeal(RenderContext *context, Point center, float size, const char *text,
                      D2D1_COLOR_F color, float rotation, float fontSize)
{
    Rect rect = MakeRect(center.x - size * 0.5f, center.y - size * 0.5f, size, size);
    const float radius = size * 0.1f;
    TextStyle style = LabelStyle(fontSize, DWRITE_FONT_WEIGHT_BOLD);
    Rect inner;

    style.family = FONT_FAMILY_KAI;

    RenderContext_PushRotation(context, rotation, center);
    inner = Inset(&rect, size * 0.08f);
    RenderContext_DrawShadow(context, &inner, size * 0.08f, WithAlpha(kBlack, 0.35f));
    {
        const Point from = MakePoint(rect.x, rect.y);
        const Point to = MakePoint(rect.x + size, rect.y + size);
        GradientStop stops[2];

        stops[0] = Stop(0.0f, LerpColor(color, kWhite, 0.08f));
        stops[1] = Stop(1.0f, LerpColor(color, kBlack, 0.18f));
        RenderContext_FillRoundedRectBrush(context, &rect, radius,
                                           RenderContext_Linear(context, from, to, stops, 2));
    }
    RenderContext_PushOpacity(context, 0.45f);
    RenderContext_FillRoundedRectBrush(context, &rect, radius, RenderContext_FeltBrush(context));
    RenderContext_PopOpacity(context);
    inner = Inset(&rect, size * 0.075f);
    RenderContext_StrokeRoundedRect(context, &inner, radius * 0.6f, WithAlpha(THEME_IVORY, 0.82f),
                                    MaxF(1.0f, size * 0.03f));
    if (strchr(text, '\n') != NULL) {
        style.lineHeight = fontSize * 1.02f;
    }
    inner = MakeRect(rect.x, rect.y + size * 0.02f, rect.width, rect.height);
    RenderContext_DrawTextUtf8(context, text, &inner, &style, WithAlpha(THEME_IVORY, 0.95f));
    RenderContext_PopTransform(context);
}

void Widgets_DrawProgressBar(RenderContext *context, const Rect *rect, float value, float time)
{
    const float radius = rect->height * 0.5f;
    Rect fill;
    float sweep;
    float sx;
    Rect shine;
    Point from;
    Point to;

    RenderContext_FillRoundedRect(context, rect, radius, WithAlpha(THEME_INK, 0.9f));
    RenderContext_StrokeRoundedRect(context, rect, radius, WithAlpha(THEME_GOLD, 0.22f), 1.0f);
    fill = MakeRect(rect->x, rect->y, MaxF(rect->height, rect->width * Clamp01(value)),
                    rect->height);
    from = MakePoint(fill.x, fill.y);
    to = MakePoint(fill.x + fill.width, fill.y);
    {
        GradientStop stops[2];

        stops[0] = Stop(0.0f, THEME_GOLD_DEEP);
        stops[1] = Stop(1.0f, THEME_GOLD_LIGHT);
        RenderContext_FillRoundedRectBrush(context, &fill, radius,
                                           RenderContext_Linear(context, from, to, stops, 2));
    }
    sweep = fmodf(time * 0.9f, 1.4f) - 0.2f;
    sx = fill.x + fill.width * sweep;
    shine = MakeRect(sx - 40.0f, fill.y, 80.0f, fill.height);
    RenderContext_PushClip(context, &fill);
    {
        const Point shineFrom = MakePoint(sx - 40.0f, fill.y);
        const Point shineTo = MakePoint(sx + 40.0f, fill.y);
        GradientStop stops[3];

        stops[0] = Stop(0.0f, WithAlpha(kWhite, 0.0f));
        stops[1] = Stop(0.5f, WithAlpha(kWhite, 0.55f));
        stops[2] = Stop(1.0f, WithAlpha(kWhite, 0.0f));
        RenderContext_FillRectBrush(context, &shine,
                                    RenderContext_Linear(context, shineFrom, shineTo, stops, 3));
    }
    RenderContext_PopClip(context);
}

void Widgets_DrawVignette(RenderContext *context, float strength)
{
    const Rect full = MakeRect(0.0f, 0.0f, LogicalWidth, LogicalHeight);
    const Point focus = MakePoint(640.0f, 360.0f);
    GradientStop stops[3];

    stops[0] = Stop(0.0f, WithAlpha(THEME_ROOM_DEEP, 0.0f));
    stops[1] = Stop(0.62f, WithAlpha(THEME_ROOM_DEEP, 0.0f));
    stops[2] = Stop(1.0f, WithAlpha(THEME_ROOM_DEEP, strength));
    RenderContext_FillRectBrush(context, &full,
                                RenderContext_Radial(context, focus, 860.0f, 560.0f, stops, 3));
}

void Widgets_DrawRoomBackground(RenderContext *context, Point focus)
{
    const Rect full = MakeRect(0.0f, 0.0f, LogicalWidth, LogicalHeight);
    GradientStop stops[3];

    stops[0] = Stop(0.0f, THEME_FELT);
    stops[1] = Stop(0.5f, THEME_FELT_DEEP);
    stops[2] = Stop(1.0f, THEME_ROOM);
    RenderContext_FillRectBrush(context, &full,
                                RenderContext_Radial(context, focus, 900.0f, 720.0f, stops, 3));
    RenderContext_PushOpacity(context, 0.28f);
    RenderContext_FillRectBrush(context, &full, RenderContext_FeltBrush(context));
    RenderContext_PopOpacity(context);
    Widgets_DrawVignette(context, 0.85f);
}

void Widgets_DrawBackdrop(RenderContext *context, float alpha)
{
    const Rect full = MakeRect(0.0f, 0.0f, LogicalWidth, LogicalHeight);

    RenderContext_PushOpacity(context, alpha);
    RenderContext_FillRect(context, &full, WithAlpha(THEME_ROOM_DEEP, 0.58f));
    Widgets_DrawVignette(context, 0.7f);
    RenderContext_PopOpacity(context);
}

void Widgets_DrawDialogBody(RenderContext *context, const Rect *panel, Icon icon,
                            D2D1_COLOR_F accent, const char *title, const char *subtitle)
{
    const PanelStyle style = PanelStyle_Default();
    const Point badge = MakePoint(panel->x + panel->width * 0.5f, panel->y + 52.0f);
    const Rect circle = MakeRect(badge.x - 24.0f, badge.y - 24.0f, 48.0f, 48.0f);
    Rect iconBox;
    Rect titleRect;
    Rect subtitleRect;

    Widgets_DrawPanel(context, panel, &style);
    Widgets_DrawRadialGlow(context, badge, 44.0f, WithAlpha(accent, 0.22f), 0.0f);
    RenderContext_FillEllipse(context, &circle, WithAlpha(THEME_INK, 0.9f));
    RenderContext_StrokeEllipse(context, &circle, WithAlpha(accent, 0.85f), 1.4f);
    iconBox = MakeRect(badge.x - 12.0f, badge.y - 12.0f, 24.0f, 24.0f);
    Icons_Draw(context, icon, &iconBox, accent);

    titleRect = MakeRect(panel->x, panel->y + 88.0f, panel->width, 36.0f);
    {
        TextStyle body = LabelStyle(23.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD);

        RenderContext_DrawTextUtf8(context, title, &titleRect, &body, THEME_IVORY);
    }
    subtitleRect = MakeRect(panel->x + 30.0f, panel->y + 126.0f, panel->width - 60.0f, 24.0f);
    {
        TextStyle body = LabelStyle(15.0f, DWRITE_FONT_WEIGHT_NORMAL);

        RenderContext_DrawTextUtf8(context, subtitle, &subtitleRect, &body, THEME_MUTED);
    }
}

void Widgets_BeginModal(RenderContext *context, const Rect *panel, float elapsed)
{
    const float t = Progress(elapsed, 0.0f, 0.32f);

    Widgets_DrawBackdrop(context, EaseOutCubic(Progress(elapsed, 0.0f, 0.22f)));
    RenderContext_PushOpacity(context, EaseOutCubic(Progress(elapsed, 0.0f, 0.2f)));
    RenderContext_PushScale(context, Lerp(0.93f, 1.0f, EaseOutBackWith(t, 1.4f)), Center(panel));
}

void Widgets_EndModal(RenderContext *context)
{
    RenderContext_PopTransform(context);
    RenderContext_PopOpacity(context);
}
