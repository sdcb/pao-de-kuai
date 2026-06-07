#pragma once

#include "audio/SoundIds.h"
#include "core/Geometry.h"
#include "graphics/D2DContext.h"
#include "graphics/SpriteAtlas.h"
#include "resources/CardAtlasData.h"
#include "rules/Card.h"

#include <algorithm>
#include <string>
#include <vector>

namespace pdk::scenes {

enum class ButtonShape {
    Rect,
    Circle
};

struct Button {
    core::Rect rect;
    std::string text;
    ButtonShape shape{ButtonShape::Rect};
    float fontSize{22.0f};
    bool visible{true};
    bool enabled{true};
    bool hover{false};

    void Draw(graphics::RenderContext& context) const;
    bool HitTest(float x, float y) const;
    void UpdateHover(float x, float y);
};

struct ButtonGroup {
    static void DrawAll(graphics::RenderContext& context, const std::vector<Button>& buttons);
    static int Hit(const std::vector<Button>& buttons, float x, float y);
    static void UpdateHover(std::vector<Button>& buttons, float x, float y);
};

inline D2D1_COLOR_F Color(float r, float g, float b, float a = 1.0f) {
    return D2D1::ColorF(r, g, b, a);
}

inline void DrawPanel(graphics::RenderContext& context, const core::Rect& rect, D2D1_COLOR_F fill = Color(0.05f, 0.12f, 0.10f, 0.88f)) {
    context.FillRect(rect, fill);
    context.StrokeRect(rect, Color(0.76f, 0.88f, 0.72f, 0.55f), 1.5f);
}

inline void Button::Draw(graphics::RenderContext& context) const {
    if (!visible) {
        return;
    }
    D2D1_COLOR_F fill = enabled
        ? (hover ? Color(0.86f, 0.72f, 0.28f) : Color(0.18f, 0.42f, 0.34f))
        : Color(0.16f, 0.18f, 0.17f);
    D2D1_COLOR_F stroke = enabled ? Color(0.92f, 0.87f, 0.62f) : Color(0.35f, 0.38f, 0.36f);
    D2D1_COLOR_F textColor = enabled ? Color(0.98f, 0.98f, 0.90f) : Color(0.52f, 0.55f, 0.52f);

    if (shape == ButtonShape::Circle) {
        context.FillEllipse(rect, fill);
        context.StrokeEllipse(rect, stroke, 1.5f);
    } else {
        context.FillRect(rect, fill);
        context.StrokeRect(rect, stroke, 1.5f);
    }
    context.DrawTextUtf8(text, rect, fontSize, textColor, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
}

inline bool Button::HitTest(float x, float y) const {
    if (!visible || !enabled) {
        return false;
    }
    if (shape == ButtonShape::Circle) {
        const float cx = rect.x + rect.width * 0.5f;
        const float cy = rect.y + rect.height * 0.5f;
        const float radius = std::min(rect.width, rect.height) * 0.5f;
        const float dx = x - cx;
        const float dy = y - cy;
        return dx * dx + dy * dy <= radius * radius;
    }
    return rect.Contains(x, y);
}

inline void Button::UpdateHover(float x, float y) {
    hover = HitTest(x, y);
}

inline void ButtonGroup::DrawAll(graphics::RenderContext& context, const std::vector<Button>& buttons) {
    for (const Button& button : buttons) {
        button.Draw(context);
    }
}

inline int ButtonGroup::Hit(const std::vector<Button>& buttons, float x, float y) {
    for (std::size_t i = 0; i < buttons.size(); ++i) {
        if (buttons[i].HitTest(x, y)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

inline void ButtonGroup::UpdateHover(std::vector<Button>& buttons, float x, float y) {
    for (Button& button : buttons) {
        button.UpdateHover(x, y);
    }
}

inline void DrawButton(graphics::RenderContext& context, const Button& button) {
    button.Draw(context);
}

inline void DrawSlider(graphics::RenderContext& context, const core::Rect& rect, float value) {
    context.FillRect({rect.x, rect.y + rect.height * 0.45f, rect.width, rect.height * 0.10f}, Color(0.32f, 0.42f, 0.38f));
    context.FillRect({rect.x, rect.y + rect.height * 0.45f, rect.width * value, rect.height * 0.10f}, Color(0.86f, 0.72f, 0.28f));
    const float knobX = rect.x + rect.width * value - 8.0f;
    context.FillRect({knobX, rect.y + 4.0f, 16.0f, rect.height - 8.0f}, Color(0.96f, 0.93f, 0.76f));
}

inline void DrawCard(
    graphics::RenderContext& context,
    graphics::SpriteAtlas& atlas,
    const rules::Card& card,
    const core::Rect& rect,
    bool selected = false,
    bool hover = false,
    bool hint = false) {
    core::Rect drawRect = rect;
    if (selected) {
        drawRect.y -= 18.0f;
    }
    if (hint) {
        context.FillRect({drawRect.x - 4.0f, drawRect.y - 4.0f, drawRect.width + 8.0f, drawRect.height + 8.0f}, Color(0.95f, 0.78f, 0.22f, 0.50f));
    }
    if (atlas.Loaded()) {
        const D2D1_RECT_U src = resources::CardSourceRect(card);
        context.DrawBitmap(atlas.Bitmap(), drawRect, &src);
    } else {
        context.FillRect(drawRect, Color(0.96f, 0.94f, 0.86f));
        context.StrokeRect(drawRect, Color(0.08f, 0.12f, 0.10f), 1.0f);
        context.DrawTextUtf8(rules::ToString(card), drawRect, 18.0f, Color(0.08f, 0.10f, 0.08f), DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER);
    }
    if (hover) {
        context.StrokeRect(drawRect, Color(0.98f, 0.88f, 0.32f), 3.0f);
    }
}

inline void DrawCardBack(graphics::RenderContext& context, graphics::SpriteAtlas& atlas, const core::Rect& rect) {
    if (atlas.Loaded()) {
        const D2D1_RECT_U src = resources::CardBackSourceRect();
        context.DrawBitmap(atlas.Bitmap(), rect, &src);
    } else {
        context.FillRect(rect, Color(0.18f, 0.32f, 0.58f));
        context.StrokeRect(rect, Color(0.92f, 0.93f, 0.85f), 1.0f);
    }
}

inline int HitButton(const std::vector<Button>& buttons, float x, float y) {
    return ButtonGroup::Hit(buttons, x, y);
}

inline void UpdateButtonHover(std::vector<Button>& buttons, float x, float y) {
    ButtonGroup::UpdateHover(buttons, x, y);
}

} // namespace pdk::scenes
