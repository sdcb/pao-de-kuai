#pragma once

/*
 * Logical layout geometry for the fixed 1280x720 design (AGENTS.md).
 *
 * Pure C, but also consumed by the C++ translation units that are still part of
 * the port, so this header stays inside the common subset of C17 and C++17:
 * no compound literals, no designated initializers, no member functions.
 * The old `Rect::Contains` member became `Rect_Contains` and the old default
 * member initialisers became explicit `*_Identity` helpers, because C has
 * neither.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LogicalWidth 1280.0f
#define LogicalHeight 720.0f

typedef struct Point {
    float x;
    float y;
} Point;

typedef struct Size {
    float width;
    float height;
} Size;

typedef struct Rect {
    float x;
    float y;
    float width;
    float height;
} Rect;

typedef struct ViewTransform {
    float scale;
    float offsetX;
    float offsetY;
} ViewTransform;

static inline bool Rect_Contains(const Rect *rect, float px, float py)
{
    return px >= rect->x && px <= rect->x + rect->width &&
           py >= rect->y && py <= rect->y + rect->height;
}

/* Replacement for `ViewTransform{}`'s old scale{1.0f} default. */
static inline ViewTransform ViewTransform_Identity(void)
{
    ViewTransform out;
    out.scale = 1.0f;
    out.offsetX = 0.0f;
    out.offsetY = 0.0f;
    return out;
}

static inline ViewTransform ComputeViewTransform(int pixelWidth, int pixelHeight)
{
    ViewTransform out;
    const float sx = (float)pixelWidth / LogicalWidth;
    const float sy = (float)pixelHeight / LogicalHeight;
    const float scale = sx < sy ? sx : sy;
    out.scale = scale;
    out.offsetX = ((float)pixelWidth - LogicalWidth * scale) * 0.5f;
    out.offsetY = ((float)pixelHeight - LogicalHeight * scale) * 0.5f;
    return out;
}

static inline Point ToLogical(Point physical, const ViewTransform *transform)
{
    Point out;
    out.x = (physical.x - transform->offsetX) / transform->scale;
    out.y = (physical.y - transform->offsetY) / transform->scale;
    return out;
}

#ifdef __cplusplus
}
#endif
