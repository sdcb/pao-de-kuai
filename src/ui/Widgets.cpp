#include "ui/Widgets.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pdk::ui {
namespace {

constexpr D2D1_COLOR_F Black = {0.0f, 0.0f, 0.0f, 1.0f};
constexpr D2D1_COLOR_F White = {1.0f, 1.0f, 1.0f, 1.0f};

core::Point Center(const core::Rect& rect) {
    return {rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f};
}

core::Rect Offset(const core::Rect& rect, float dx, float dy) {
    return {rect.x + dx, rect.y + dy, rect.width, rect.height};
}

core::Rect Inset(const core::Rect& rect, float d) {
    return {rect.x + d, rect.y + d, rect.width - d * 2.0f, rect.height - d * 2.0f};
}

void DrawButtonLabel(graphics::RenderContext& context, const Button& button, D2D1_COLOR_F color) {
    graphics::TextStyle style = Centered(Text(button.fontSize, DWRITE_FONT_WEIGHT_SEMI_BOLD));
    style.wrap = false;
    if (button.icon == Icon::None) {
        context.DrawTextUtf8(button.text, button.rect, style, color);
        return;
    }
    const float iconSize = std::min(button.rect.height * 0.46f, 22.0f);
    if (button.text.empty()) {
        const core::Point c = Center(button.rect);
        DrawIcon(context, button.icon, {c.x - iconSize * 0.5f, c.y - iconSize * 0.5f, iconSize, iconSize}, color);
        return;
    }
    const float textWidth = context.MeasureText(button.text, style).width;
    const float gap = 7.0f;
    const float total = iconSize + gap + textWidth;
    const float left = button.rect.x + (button.rect.width - total) * 0.5f;
    DrawIcon(context, button.icon, {left, button.rect.y + (button.rect.height - iconSize) * 0.5f, iconSize, iconSize}, color);
    style.align = DWRITE_TEXT_ALIGNMENT_LEADING;
    context.DrawTextUtf8(button.text, {left + iconSize + gap, button.rect.y, textWidth + 4.0f, button.rect.height}, style, color);
}

} // namespace

void Button::Update(float dt) {
    hoverT = Approach(hoverT, hover && enabled && visible ? 1.0f : 0.0f, 16.0f, dt);
    pressT = Approach(pressT, 0.0f, 12.0f, dt);
    visibleT = Approach(visibleT, visible ? 1.0f : 0.0f, 14.0f, dt);
}

void Button::Draw(graphics::RenderContext& context) const {
    if (!visible && visibleT < 0.02f) {
        return;
    }
    const float h = enabled ? hoverT : 0.0f;
    const core::Point center = Center(rect);
    const float radius = rect.height * 0.5f;

    context.PushOpacity((enabled ? 1.0f : 0.42f) * (visible ? std::max(visibleT, 0.02f) : visibleT));
    context.PushScale(1.0f + 0.018f * h - 0.04f * pressT, center);
    context.PushTranslation(0.0f, -1.5f * h);

    switch (style) {
    case ButtonStyle::Primary: {
        context.DrawShadow(Offset(Inset(rect, 3.0f), 0.0f, 5.0f), 7.0f, WithAlpha(Black, 0.55f));
        if (h > 0.01f) {
            context.DrawShadow(rect, 10.0f, WithAlpha(theme::Gold, 0.40f * h));
        }
        const D2D1_COLOR_F top = LerpColor(theme::GoldLight, White, 0.25f * h);
        context.FillRoundedRect(rect, radius, context.Linear(
            {rect.x, rect.y}, {rect.x, rect.y + rect.height},
            {{0.0f, top}, {0.55f, LerpColor(theme::Gold, theme::GoldLight, 0.3f * h)}, {1.0f, theme::GoldDeep}}));
        context.StrokeRoundedRect(Inset(rect, 0.5f), radius, WithAlpha(theme::GoldDeep, 0.9f), 1.0f);
        context.DrawLine({rect.x + radius, rect.y + 1.6f}, {rect.x + rect.width - radius, rect.y + 1.6f}, WithAlpha(White, 0.45f), 1.0f);
        DrawButtonLabel(context, *this, theme::GoldInk);
        break;
    }
    case ButtonStyle::Secondary: {
        context.DrawShadow(Offset(Inset(rect, 3.0f), 0.0f, 4.0f), 6.0f, WithAlpha(Black, 0.45f));
        context.FillRoundedRect(rect, radius, context.Linear(
            {rect.x, rect.y}, {rect.x, rect.y + rect.height},
            {{0.0f, WithAlpha(LerpColor(theme::InkRaised, theme::InkHover, h), 0.96f)}, {1.0f, WithAlpha(theme::Ink, 0.96f)}}));
        context.StrokeRoundedRect(Inset(rect, 0.5f), radius, WithAlpha(theme::Gold, 0.36f + 0.5f * h), 1.0f);
        DrawButtonLabel(context, *this, LerpColor(theme::Ivory, theme::GoldLight, h));
        break;
    }
    case ButtonStyle::Ghost: {
        context.FillRoundedRect(rect, radius, WithAlpha(White, 0.03f + 0.06f * h));
        context.StrokeRoundedRect(Inset(rect, 0.5f), radius, WithAlpha(theme::Ivory, 0.22f + 0.3f * h), 1.0f);
        DrawButtonLabel(context, *this, LerpColor(theme::Muted, theme::Ivory, h));
        break;
    }
    case ButtonStyle::Danger: {
        context.DrawShadow(Offset(Inset(rect, 3.0f), 0.0f, 5.0f), 7.0f, WithAlpha(Black, 0.5f));
        if (h > 0.01f) {
            context.DrawShadow(rect, 10.0f, WithAlpha(theme::Cinnabar, 0.45f * h));
        }
        context.FillRoundedRect(rect, radius, context.Linear(
            {rect.x, rect.y}, {rect.x, rect.y + rect.height},
            {{0.0f, LerpColor(theme::CinnabarLight, White, 0.12f * h)}, {0.6f, theme::Cinnabar}, {1.0f, theme::CinnabarDeep}}));
        context.StrokeRoundedRect(Inset(rect, 0.5f), radius, WithAlpha(theme::CinnabarDeep, 0.9f), 1.0f);
        context.DrawLine({rect.x + radius, rect.y + 1.6f}, {rect.x + rect.width - radius, rect.y + 1.6f}, WithAlpha(White, 0.28f), 1.0f);
        DrawButtonLabel(context, *this, theme::Ivory);
        break;
    }
    case ButtonStyle::Icon: {
        context.DrawShadow(Offset(Inset(rect, 4.0f), 0.0f, 3.0f), 5.0f, WithAlpha(Black, 0.5f));
        context.FillEllipse(rect, context.Linear(
            {rect.x, rect.y}, {rect.x, rect.y + rect.height},
            {{0.0f, LerpColor(theme::InkRaised, theme::InkHover, h)}, {1.0f, theme::Ink}}));
        context.StrokeEllipse(Inset(rect, 0.5f), WithAlpha(theme::Gold, 0.40f + 0.5f * h), 1.0f);
        DrawButtonLabel(context, *this, LerpColor(theme::Ivory, theme::GoldLight, h));
        break;
    }
    }

    context.PopTransform();
    context.PopTransform();
    context.PopOpacity();
}

bool Button::HitTest(float x, float y) const {
    if (!visible || !enabled) {
        return false;
    }
    if (style == ButtonStyle::Icon) {
        const core::Point c = Center(rect);
        const float r = std::min(rect.width, rect.height) * 0.5f;
        return (x - c.x) * (x - c.x) + (y - c.y) * (y - c.y) <= r * r;
    }
    return rect.Contains(x, y);
}

void Button::UpdateHover(float x, float y) {
    hover = HitTest(x, y);
}

void ButtonGroup::DrawAll(graphics::RenderContext& context, const std::vector<Button>& buttons) {
    for (const Button& button : buttons) {
        button.Draw(context);
    }
}

void ButtonGroup::UpdateAll(std::vector<Button>& buttons, float dt) {
    for (Button& button : buttons) {
        button.Update(dt);
    }
}

int ButtonGroup::Hit(std::vector<Button>& buttons, float x, float y) {
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (buttons[i].HitTest(x, y)) {
            buttons[i].pressT = 1.0f;
            return static_cast<int>(i);
        }
    }
    return -1;
}

void ButtonGroup::UpdateHover(std::vector<Button>& buttons, float x, float y) {
    for (Button& button : buttons) {
        button.UpdateHover(x, y);
    }
}

void DrawPanel(graphics::RenderContext& context, const core::Rect& rect, const PanelStyle& style) {
    if (style.shadow > 0.0f) {
        context.DrawShadow(Offset(Inset(rect, 6.0f), 0.0f, 12.0f), 18.0f, WithAlpha(Black, 0.55f * style.shadow));
    }
    context.FillRoundedRect(rect, style.radius, context.Linear(
        {rect.x, rect.y}, {rect.x, rect.y + rect.height},
        {{0.0f, WithAlpha(style.top, style.fillAlpha)}, {1.0f, WithAlpha(style.bottom, style.fillAlpha)}}));
    context.StrokeRoundedRect(Inset(rect, 0.5f), style.radius, context.Linear(
        {rect.x, rect.y}, {rect.x, rect.y + rect.height},
        {{0.0f, WithAlpha(theme::Gold, 0.62f)}, {0.5f, WithAlpha(theme::Gold, 0.28f)}, {1.0f, WithAlpha(theme::Gold, 0.44f)}}), 1.0f);
    if (style.innerLine) {
        context.StrokeRoundedRect(Inset(rect, 5.5f), std::max(2.0f, style.radius - 5.0f), WithAlpha(theme::Gold, 0.10f), 1.0f);
    }
    if (style.ornament) {
        DrawOrnamentCorners(context, rect, WithAlpha(theme::Gold, 0.55f));
    }
}

void DrawOrnamentCorners(graphics::RenderContext& context, const core::Rect& rect, D2D1_COLOR_F color, float size, float inset) {
    // A small 回-fret hook mirrored into each corner.
    const std::array<core::Point, 3> outer{core::Point{0.0f, size}, core::Point{0.0f, 0.0f}, core::Point{size, 0.0f}};
    const float s = size;
    const std::array<core::Point, 4> hook{
        core::Point{4.5f, s - 5.0f}, core::Point{4.5f, 4.5f}, core::Point{s - 5.0f, 4.5f}, core::Point{s - 5.0f, 9.0f}};
    const std::array<std::array<float, 4>, 4> corners{{
        {rect.x + inset, rect.y + inset, 1.0f, 1.0f},
        {rect.x + rect.width - inset, rect.y + inset, -1.0f, 1.0f},
        {rect.x + inset, rect.y + rect.height - inset, 1.0f, -1.0f},
        {rect.x + rect.width - inset, rect.y + rect.height - inset, -1.0f, -1.0f}}};
    for (const auto& corner : corners) {
        std::array<core::Point, 3> a{};
        std::array<core::Point, 4> b{};
        for (std::size_t i = 0; i < outer.size(); ++i) {
            a[i] = {corner[0] + outer[i].x * corner[2], corner[1] + outer[i].y * corner[3]};
        }
        for (std::size_t i = 0; i < hook.size(); ++i) {
            b[i] = {corner[0] + hook[i].x * corner[2], corner[1] + hook[i].y * corner[3]};
        }
        context.StrokePolyline(a, color, 1.2f);
        context.StrokePolyline(b, WithAlpha(color, 0.7f), 1.0f);
    }
}

void DrawHairline(graphics::RenderContext& context, float x0, float x1, float y, float alpha) {
    context.DrawLine({x0, y}, {x1, y}, context.Linear({x0, y}, {x1, y},
        {{0.0f, WithAlpha(theme::Gold, 0.0f)}, {0.5f, WithAlpha(theme::Gold, alpha)}, {1.0f, WithAlpha(theme::Gold, 0.0f)}}), 1.0f);
}

core::Rect ChipRect(graphics::RenderContext& context, core::Point anchor, Anchor align, const std::string& text, const ChipStyle& style) {
    graphics::TextStyle textStyle = Text(style.fontSize, style.weight);
    textStyle.wrap = false;
    const float width = context.MeasureText(text, textStyle).width + style.padX * 2.0f;
    float x = anchor.x;
    if (align == Anchor::Center) {
        x -= width * 0.5f;
    } else if (align == Anchor::Right) {
        x -= width;
    }
    return {x, anchor.y - style.height * 0.5f, width, style.height};
}

void DrawChip(graphics::RenderContext& context, const core::Rect& rect, const std::string& text, const ChipStyle& style) {
    const float radius = rect.height * 0.5f;
    context.FillRoundedRect(rect, radius, style.fill);
    if (style.stroke.a > 0.0f) {
        context.StrokeRoundedRect(Inset(rect, 0.5f), radius, style.stroke, 1.0f);
    }
    graphics::TextStyle textStyle = Centered(Text(style.fontSize, style.weight));
    textStyle.wrap = false;
    context.DrawTextUtf8(text, rect, textStyle, style.text);
}

core::Rect DrawChip(graphics::RenderContext& context, core::Point anchor, Anchor align, const std::string& text, const ChipStyle& style) {
    const core::Rect rect = ChipRect(context, anchor, align, text, style);
    DrawChip(context, rect, text, style);
    return rect;
}

std::string AvatarLabel(const std::string& name) {
    if (name.empty()) {
        return "?";
    }
    const auto lead = static_cast<unsigned char>(name[0]);
    if (lead < 0x80) {
        return name.substr(0, std::min<std::size_t>(3, name.size()));
    }
    std::size_t length = 1;
    if ((lead & 0xE0u) == 0xC0u) {
        length = 2;
    } else if ((lead & 0xF0u) == 0xE0u) {
        length = 3;
    } else if ((lead & 0xF8u) == 0xF0u) {
        length = 4;
    }
    return name.substr(0, std::min(length, name.size()));
}

void DrawRadialGlow(graphics::RenderContext& context, core::Point center, float radius, D2D1_COLOR_F color, float ring) {
    // Keep the alpha out of the gradient stops and drive it with opacity:
    // animated alphas would otherwise mint a fresh cached brush every frame.
    const D2D1_COLOR_F solid = {color.r, color.g, color.b, 1.0f};
    const D2D1_COLOR_F clear = {color.r, color.g, color.b, 0.0f};
    context.PushOpacity(color.a);
    ID2D1Brush* brush = ring > 0.0f
        ? context.Radial(center, radius, radius, {{0.0f, clear}, {ring * 0.82f, clear}, {ring, solid}, {1.0f, clear}})
        : context.Radial(center, radius, radius, {{0.0f, solid}, {1.0f, clear}});
    context.FillEllipse({center.x - radius, center.y - radius, radius * 2.0f, radius * 2.0f}, brush);
    context.PopOpacity();
}

void DrawAvatar(graphics::RenderContext& context, const core::Rect& rect, const std::string& label, bool active, float time) {
    const core::Point c = Center(rect);
    const float r = rect.width * 0.5f;
    if (active) {
        const float pulse = 0.5f + 0.5f * std::sin(time * 4.2f);
        DrawRadialGlow(context, c, r * (1.55f + 0.12f * pulse), WithAlpha(theme::Gold, 0.55f + 0.3f * pulse), 0.68f);
    }
    context.DrawShadow({rect.x + 4.0f, rect.y + 7.0f, rect.width - 8.0f, rect.height - 8.0f}, 5.0f, WithAlpha(Black, 0.55f));
    context.FillEllipse(rect, context.Radial({c.x - r * 0.35f, c.y - r * 0.45f}, r * 1.6f, r * 1.6f,
        {{0.0f, theme::InkHover}, {1.0f, theme::Ink}}));
    context.StrokeEllipse(Inset(rect, 0.75f), active ? theme::GoldLight : WithAlpha(theme::Gold, 0.7f), active ? 2.0f : 1.3f);
    context.StrokeEllipse(Inset(rect, 4.0f), WithAlpha(theme::Gold, 0.18f), 1.0f);
    const bool ascii = !label.empty() && static_cast<unsigned char>(label[0]) < 0x80;
    graphics::TextStyle style = ascii ? Centered(Text(r * 0.62f, DWRITE_FONT_WEIGHT_SEMI_BOLD)) : Centered(Kai(r * 0.95f));
    style.wrap = false;
    context.DrawTextUtf8(label, rect, style, theme::GoldLight);
}

void DrawSeal(graphics::RenderContext& context, core::Point center, float size, const std::string& text, D2D1_COLOR_F color, float rotation, float fontSize) {
    const core::Rect rect{center.x - size * 0.5f, center.y - size * 0.5f, size, size};
    const float radius = size * 0.1f;
    context.PushRotation(rotation, center);
    context.DrawShadow(Inset(rect, size * 0.08f), size * 0.08f, WithAlpha(Black, 0.35f));
    context.FillRoundedRect(rect, radius, context.Linear({rect.x, rect.y}, {rect.x + size, rect.y + size},
        {{0.0f, LerpColor(color, White, 0.08f)}, {1.0f, LerpColor(color, Black, 0.18f)}}));
    context.PushOpacity(0.45f);
    context.FillRoundedRect(rect, radius, context.FeltBrush());
    context.PopOpacity();
    context.StrokeRoundedRect(Inset(rect, size * 0.075f), radius * 0.6f, WithAlpha(theme::Ivory, 0.82f), std::max(1.0f, size * 0.03f));
    graphics::TextStyle style = Centered(Kai(fontSize));
    if (text.find('\n') != std::string::npos) {
        style.lineHeight = fontSize * 1.02f;
    }
    context.DrawTextUtf8(text, {rect.x, rect.y + size * 0.02f, rect.width, rect.height}, style, WithAlpha(theme::Ivory, 0.95f));
    context.PopTransform();
}

void DrawProgressBar(graphics::RenderContext& context, const core::Rect& rect, float value, float time) {
    const float radius = rect.height * 0.5f;
    context.FillRoundedRect(rect, radius, WithAlpha(theme::Ink, 0.9f));
    context.StrokeRoundedRect(rect, radius, WithAlpha(theme::Gold, 0.22f), 1.0f);
    const core::Rect fill{rect.x, rect.y, std::max(rect.height, rect.width * Clamp01(value)), rect.height};
    context.FillRoundedRect(fill, radius, context.Linear({fill.x, fill.y}, {fill.x + fill.width, fill.y},
        {{0.0f, theme::GoldDeep}, {1.0f, theme::GoldLight}}));
    const float sweep = std::fmod(time * 0.9f, 1.4f) - 0.2f;
    const float sx = fill.x + fill.width * sweep;
    context.PushClip(fill);
    context.FillRect({sx - 40.0f, fill.y, 80.0f, fill.height}, context.Linear({sx - 40.0f, fill.y}, {sx + 40.0f, fill.y},
        {{0.0f, WithAlpha(White, 0.0f)}, {0.5f, WithAlpha(White, 0.55f)}, {1.0f, WithAlpha(White, 0.0f)}}));
    context.PopClip();
}

void DrawVignette(graphics::RenderContext& context, float strength) {
    context.FillRect({0.0f, 0.0f, core::LogicalWidth, core::LogicalHeight},
        context.Radial({640.0f, 360.0f}, 860.0f, 560.0f,
            {{0.0f, WithAlpha(theme::RoomDeep, 0.0f)}, {0.62f, WithAlpha(theme::RoomDeep, 0.0f)}, {1.0f, WithAlpha(theme::RoomDeep, strength)}}));
}

void DrawRoomBackground(graphics::RenderContext& context, core::Point focus) {
    const core::Rect full{0.0f, 0.0f, core::LogicalWidth, core::LogicalHeight};
    context.FillRect(full, context.Radial(focus, 900.0f, 720.0f,
        {{0.0f, theme::Felt}, {0.5f, theme::FeltDeep}, {1.0f, theme::Room}}));
    context.PushOpacity(0.28f);
    context.FillRect(full, context.FeltBrush());
    context.PopOpacity();
    DrawVignette(context, 0.85f);
}

void DrawBackdrop(graphics::RenderContext& context, float alpha) {
    context.PushOpacity(alpha);
    context.FillRect({0.0f, 0.0f, core::LogicalWidth, core::LogicalHeight}, WithAlpha(theme::RoomDeep, 0.58f));
    DrawVignette(context, 0.7f);
    context.PopOpacity();
}

void DrawDialogBody(graphics::RenderContext& context, const core::Rect& panel, Icon icon, D2D1_COLOR_F accent,
    const std::string& title, const std::string& subtitle) {
    DrawPanel(context, panel);
    const core::Point badge{panel.x + panel.width * 0.5f, panel.y + 52.0f};
    DrawRadialGlow(context, badge, 44.0f, WithAlpha(accent, 0.22f));
    const core::Rect circle{badge.x - 24.0f, badge.y - 24.0f, 48.0f, 48.0f};
    context.FillEllipse(circle, WithAlpha(theme::Ink, 0.9f));
    context.StrokeEllipse(circle, WithAlpha(accent, 0.85f), 1.4f);
    DrawIcon(context, icon, {badge.x - 12.0f, badge.y - 12.0f, 24.0f, 24.0f}, accent);
    context.DrawTextUtf8(title, {panel.x, panel.y + 88.0f, panel.width, 36.0f}, Centered(Text(23.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD)), theme::Ivory);
    context.DrawTextUtf8(subtitle, {panel.x + 30.0f, panel.y + 126.0f, panel.width - 60.0f, 24.0f}, Centered(Text(15.0f)), theme::Muted);
}

void BeginModal(graphics::RenderContext& context, const core::Rect& panel, float elapsed) {
    DrawBackdrop(context, EaseOutCubic(Progress(elapsed, 0.0f, 0.22f)));
    const float t = Progress(elapsed, 0.0f, 0.32f);
    context.PushOpacity(EaseOutCubic(Progress(elapsed, 0.0f, 0.2f)));
    context.PushScale(Lerp(0.93f, 1.0f, EaseOutBack(t, 1.4f)), Center(panel));
}

void EndModal(graphics::RenderContext& context) {
    context.PopTransform();
    context.PopOpacity();
}

} // namespace pdk::ui
