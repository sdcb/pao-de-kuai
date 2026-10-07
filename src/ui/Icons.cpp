#include "ui/Icons.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace pdk::ui {
namespace {

Point At(const Rect& box, float u, float v) {
    return {box.x + box.width * u, box.y + box.height * v};
}

Rect SquareIn(const Rect& rect) {
    const float side = std::min(rect.width, rect.height);
    return {rect.x + (rect.width - side) * 0.5f, rect.y + (rect.height - side) * 0.5f, side, side};
}

void Star(graphics::RenderContext& context, Point center, float outer, float inner, D2D1_COLOR_F color) {
    std::array<Point, 10> points{};
    for (int i = 0; i < 10; ++i) {
        const float radius = (i % 2 == 0) ? outer : inner;
        const float angle = -1.5707963f + static_cast<float>(i) * 0.62831853f;
        points[static_cast<std::size_t>(i)] = {center.x + std::cos(angle) * radius, center.y + std::sin(angle) * radius};
    }
    context.FillPolygon(points, color);
}

} // namespace

void DrawIcon(graphics::RenderContext& context, Icon icon, const Rect& rect, D2D1_COLOR_F color) {
    const Rect box = SquareIn(rect);
    const float stroke = std::max(1.6f, box.width * 0.11f);
    switch (icon) {
    case Icon::None:
        break;
    case Icon::Back: {
        const std::array<Point, 3> chevron{At(box, 0.60f, 0.20f), At(box, 0.30f, 0.50f), At(box, 0.60f, 0.80f)};
        context.StrokePolyline(chevron, color, stroke);
        break;
    }
    case Icon::Close:
        context.DrawLine(At(box, 0.25f, 0.25f), At(box, 0.75f, 0.75f), color, stroke);
        context.DrawLine(At(box, 0.75f, 0.25f), At(box, 0.25f, 0.75f), color, stroke);
        break;
    case Icon::Star:
        Star(context, At(box, 0.5f, 0.54f), box.width * 0.48f, box.width * 0.2f, color);
        break;
    case Icon::Exit: {
        const std::array<Point, 4> door{At(box, 0.55f, 0.18f), At(box, 0.22f, 0.18f), At(box, 0.22f, 0.82f), At(box, 0.55f, 0.82f)};
        context.StrokePolyline(door, color, stroke);
        context.DrawLine(At(box, 0.42f, 0.5f), At(box, 0.86f, 0.5f), color, stroke);
        const std::array<Point, 3> arrow{At(box, 0.70f, 0.34f), At(box, 0.86f, 0.5f), At(box, 0.70f, 0.66f)};
        context.StrokePolyline(arrow, color, stroke);
        break;
    }
    case Icon::Alert: {
        context.StrokeEllipse({box.x + stroke * 0.5f, box.y + stroke * 0.5f, box.width - stroke, box.height - stroke}, color, stroke * 0.8f);
        context.DrawLine(At(box, 0.5f, 0.26f), At(box, 0.5f, 0.58f), color, stroke);
        context.FillEllipse({box.x + box.width * 0.5f - stroke * 0.6f, box.y + box.height * 0.72f - stroke * 0.6f, stroke * 1.2f, stroke * 1.2f}, color);
        break;
    }
    case Icon::Sparkle: {
        const std::array<Point, 8> points{
            At(box, 0.5f, 0.0f), At(box, 0.6f, 0.4f), At(box, 1.0f, 0.5f), At(box, 0.6f, 0.6f),
            At(box, 0.5f, 1.0f), At(box, 0.4f, 0.6f), At(box, 0.0f, 0.5f), At(box, 0.4f, 0.4f)};
        context.FillPolygon(points, color);
        break;
    }
    }
}

} // namespace pdk::ui
