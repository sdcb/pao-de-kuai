#include "ui/CardView.h"

#include "resources/CppCompat.h"
#include "ui/Anim.h"

#include <algorithm>

namespace pdk::ui {
namespace {

constexpr D2D1_COLOR_F Black = {0.0f, 0.0f, 0.0f, 1.0f};
constexpr D2D1_COLOR_F White = {1.0f, 1.0f, 1.0f, 1.0f};

template <typename DrawContent>
void DrawCardCommon(graphics::RenderContext& context, const Rect& rect, const CardLook& look, DrawContent&& content) {
    Rect r = rect;
    r.y -= look.lift;
    const Point center{r.x + r.width * 0.5f, r.y + r.height * 0.5f};
    const float radius = r.width * theme::CardRadiusRatio;
    const bool rotated = look.rotation != 0.0f;
    if (rotated) {
        context.PushRotation(look.rotation, center);
    }
    context.PushOpacity(look.opacity);

    if (look.shadow > 0.0f) {
        const float depth = std::max(0.0f, look.lift);
        const float blur = std::max(2.0f, r.width * 0.035f) + depth * 0.14f;
        context.DrawShadow({r.x + 1.0f, r.y + r.width * 0.03f + depth * 0.22f, r.width - 2.0f, r.height - 1.0f}, blur,
            WithAlpha(Black, (0.42f - std::min(0.14f, depth * 0.004f)) * look.shadow));
    }
    if (look.glow > 0.0f) {
        context.DrawShadow({r.x - 1.0f, r.y - 1.0f, r.width + 2.0f, r.height + 2.0f}, 7.0f, WithAlpha(look.glowColor, 0.85f * look.glow));
    }

    content(r);

    if (look.hover > 0.0f) {
        context.FillRoundedRect(r, radius, WithAlpha(White, 0.10f * look.hover));
    }
    if (look.selected) {
        context.StrokeRoundedRect({r.x + 0.75f, r.y + 0.75f, r.width - 1.5f, r.height - 1.5f}, radius, WithAlpha(theme::Gold, 0.95f), 1.6f);
    }
    if (look.glow > 0.0f) {
        context.StrokeRoundedRect({r.x + 0.75f, r.y + 0.75f, r.width - 1.5f, r.height - 1.5f}, radius, WithAlpha(look.glowColor, look.glow), 2.0f);
    }

    context.PopOpacity();
    if (rotated) {
        context.PopTransform();
    }
}

void DrawAtlasRect(graphics::RenderContext& context, graphics::SpriteAtlas& atlas, D2D1_RECT_U source, const Rect& dest) {
    const resources::CardAtlasInfo& info = resources::GetCardAtlasInfo();
    const float requested = dest.width * context.View().scale / static_cast<float>(info.cardWidth);
    float levelScale = 1.0f;
    ID2D1Bitmap* bitmap = atlas.BitmapFor(requested, levelScale);
    // Half-texel inset keeps bilinear sampling from bleeding into the neighbouring card.
    const D2D1_RECT_F src = D2D1::RectF(
        static_cast<float>(source.left) * levelScale + 0.5f,
        static_cast<float>(source.top) * levelScale + 0.5f,
        static_cast<float>(source.right) * levelScale - 0.5f,
        static_cast<float>(source.bottom) * levelScale - 0.5f);
    context.DrawBitmap(bitmap, dest, src);
}

} // namespace

void DrawCardFace(graphics::RenderContext& context, graphics::SpriteAtlas& atlas, const rules::Card& card, const Rect& rect, const CardLook& look) {
    DrawCardCommon(context, rect, look, [&](const Rect& r) {
        if (atlas.Loaded()) {
            DrawAtlasRect(context, atlas, resources::CardSourceRect(card), r);
            return;
        }
        const float radius = r.width * theme::CardRadiusRatio;
        context.FillRoundedRect(r, radius, theme::Ivory);
        context.StrokeRoundedRect(r, radius, WithAlpha(Black, 0.25f), 1.0f);
        context.DrawTextUtf8(rules::ToString(card), r, Centered(Text(r.width * 0.2f, DWRITE_FONT_WEIGHT_SEMI_BOLD)), theme::Ink);
    });
}

void DrawCardBack(graphics::RenderContext& context, graphics::SpriteAtlas& atlas, const Rect& rect, const CardLook& look) {
    DrawCardCommon(context, rect, look, [&](const Rect& r) {
        if (atlas.Loaded()) {
            DrawAtlasRect(context, atlas, resources::CardBackSourceRect(), r);
            return;
        }
        const float radius = r.width * theme::CardRadiusRatio;
        context.FillRoundedRect(r, radius, Rgb(0x1D4E6B));
        context.StrokeRoundedRect(r, radius, WithAlpha(theme::Ivory, 0.8f), 1.0f);
    });
}

} // namespace pdk::ui
