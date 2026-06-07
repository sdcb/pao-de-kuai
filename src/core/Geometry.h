#pragma once

namespace pdk::core {

constexpr float LogicalWidth = 1280.0f;
constexpr float LogicalHeight = 720.0f;

struct Point {
    float x{};
    float y{};
};

struct Size {
    float width{};
    float height{};
};

struct Rect {
    float x{};
    float y{};
    float width{};
    float height{};

    bool Contains(float px, float py) const {
        return px >= x && px <= x + width && py >= y && py <= y + height;
    }
};

struct ViewTransform {
    float scale{1.0f};
    float offsetX{0.0f};
    float offsetY{0.0f};
};

inline ViewTransform ComputeViewTransform(int pixelWidth, int pixelHeight) {
    const float sx = static_cast<float>(pixelWidth) / LogicalWidth;
    const float sy = static_cast<float>(pixelHeight) / LogicalHeight;
    const float scale = sx < sy ? sx : sy;
    const float contentW = LogicalWidth * scale;
    const float contentH = LogicalHeight * scale;
    return ViewTransform{
        scale,
        (static_cast<float>(pixelWidth) - contentW) * 0.5f,
        (static_cast<float>(pixelHeight) - contentH) * 0.5f
    };
}

inline Point ToLogical(Point physical, const ViewTransform& transform) {
    return Point{
        (physical.x - transform.offsetX) / transform.scale,
        (physical.y - transform.offsetY) / transform.scale
    };
}

} // namespace pdk::core
