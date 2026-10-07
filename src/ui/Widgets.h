#pragma once

#include "core/Geometry.h"
#include "graphics/CppCompat.h"
#include "ui/Anim.h"
#include "ui/CppCompat.h"
#include "ui/CppCompat.h"

#include <string>
#include <vector>

namespace pdk::ui {

enum class ButtonStyle {
    Primary,
    Secondary,
    Ghost,
    Danger,
    Icon
};

struct Button {
    Rect rect;
    std::string text;
    ButtonStyle style{ButtonStyle::Secondary};
    Icon icon{Icon::None};
    float fontSize{19.0f};
    bool visible{true};
    bool enabled{true};
    bool hover{false};
    float hoverT{0.0f};
    float pressT{0.0f};
    float visibleT{1.0f};

    void Update(float dt);
    void Draw(graphics::RenderContext& context) const;
    bool HitTest(float x, float y) const;
    void UpdateHover(float x, float y);
};

struct ButtonGroup {
    static void DrawAll(graphics::RenderContext& context, const std::vector<Button>& buttons);
    static void UpdateAll(std::vector<Button>& buttons, float dt);
    // Returns the hit index and starts that button's press animation.
    static int Hit(std::vector<Button>& buttons, float x, float y);
    static void UpdateHover(std::vector<Button>& buttons, float x, float y);
};

struct PanelStyle {
    float radius{theme::PanelRadius};
    float shadow{1.0f};
    float fillAlpha{0.94f};
    bool ornament{true};
    bool innerLine{true};
    D2D1_COLOR_F top{theme::InkRaised};
    D2D1_COLOR_F bottom{theme::Ink};
};

void DrawPanel(graphics::RenderContext& context, const Rect& rect, const PanelStyle& style = {});
void DrawOrnamentCorners(graphics::RenderContext& context, const Rect& rect, D2D1_COLOR_F color, float size = 16.0f, float inset = 9.0f);
// Horizontal gold line that fades out at both ends.
void DrawHairline(graphics::RenderContext& context, float x0, float x1, float y, float alpha = 0.55f);

enum class Anchor {
    Left,
    Center,
    Right
};

struct ChipStyle {
    D2D1_COLOR_F fill{WithAlpha(theme::Ink, 0.82f)};
    D2D1_COLOR_F stroke{WithAlpha(theme::Gold, 0.35f)};
    D2D1_COLOR_F text{theme::Ivory};
    float fontSize{15.0f};
    float height{28.0f};
    float padX{13.0f};
    DWRITE_FONT_WEIGHT weight{DWRITE_FONT_WEIGHT_NORMAL};
};

Rect ChipRect(graphics::RenderContext& context, Point anchor, Anchor align, const std::string& text, const ChipStyle& style);
void DrawChip(graphics::RenderContext& context, const Rect& rect, const std::string& text, const ChipStyle& style);
Rect DrawChip(graphics::RenderContext& context, Point anchor, Anchor align, const std::string& text, const ChipStyle& style = {});

// First visible character for avatars: one CJK glyph, or up to three ASCII characters.
std::string AvatarLabel(const std::string& name);
void DrawAvatar(graphics::RenderContext& context, const Rect& rect, const std::string& label, bool active, float time);
// Soft circular glow; ring > 0 turns it into a halo that peaks at that fraction of the radius.
void DrawRadialGlow(graphics::RenderContext& context, Point center, float radius, D2D1_COLOR_F color, float ring = 0.0f);

void DrawSeal(graphics::RenderContext& context, Point center, float size, const std::string& text, D2D1_COLOR_F color, float rotation, float fontSize);
void DrawProgressBar(graphics::RenderContext& context, const Rect& rect, float value, float time);

// Full-screen velvet backdrop used by the menu scenes.
void DrawRoomBackground(graphics::RenderContext& context, Point focus);
void DrawVignette(graphics::RenderContext& context, float strength);
void DrawBackdrop(graphics::RenderContext& context, float alpha);

// Compact confirm dialog: panel, round icon badge, title and one line of explanation.
void DrawDialogBody(graphics::RenderContext& context, const Rect& panel, Icon icon, D2D1_COLOR_F accent,
    const std::string& title, const std::string& subtitle);

// Modal enter animation: dims the scene, then fades and springs the panel in.
void BeginModal(graphics::RenderContext& context, const Rect& panel, float elapsed);
void EndModal(graphics::RenderContext& context);

} // namespace pdk::ui
