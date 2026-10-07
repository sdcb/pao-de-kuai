#include "ui/Icons.h"

#include <math.h>

static float MinF(float a, float b)
{
    return a < b ? a : b;
}

static float MaxF(float a, float b)
{
    return a > b ? a : b;
}

static Point At(const Rect *box, float u, float v)
{
    Point point;

    point.x = box->x + box->width * u;
    point.y = box->y + box->height * v;
    return point;
}

static Rect SquareIn(const Rect *rect)
{
    const float side = MinF(rect->width, rect->height);
    Rect square;

    square.x = rect->x + (rect->width - side) * 0.5f;
    square.y = rect->y + (rect->height - side) * 0.5f;
    square.width = side;
    square.height = side;
    return square;
}

static void Star(RenderContext *context, Point center, float outer, float inner,
                 D2D1_COLOR_F color)
{
    Point points[10];

    for (int i = 0; i < 10; ++i) {
        const float radius = (i % 2 == 0) ? outer : inner;
        const float angle = -1.5707963f + (float)i * 0.62831853f;

        points[i].x = center.x + cosf(angle) * radius;
        points[i].y = center.y + sinf(angle) * radius;
    }
    RenderContext_FillPolygon(context, points, 10, color);
}

void Icons_Draw(RenderContext *context, Icon icon, const Rect *rect, D2D1_COLOR_F color)
{
    const Rect box = SquareIn(rect);
    const float stroke = MaxF(1.6f, box.width * 0.11f);

    switch (icon) {
    case UI_ICON_NONE:
        break;
    case UI_ICON_BACK: {
        const Point chevron[3] = {At(&box, 0.60f, 0.20f), At(&box, 0.30f, 0.50f),
                                  At(&box, 0.60f, 0.80f)};

        RenderContext_StrokePolyline(context, chevron, 3, color, stroke, false);
        break;
    }
    case UI_ICON_CLOSE:
        RenderContext_DrawLine(context, At(&box, 0.25f, 0.25f), At(&box, 0.75f, 0.75f), color,
                               stroke);
        RenderContext_DrawLine(context, At(&box, 0.75f, 0.25f), At(&box, 0.25f, 0.75f), color,
                               stroke);
        break;
    case UI_ICON_STAR:
        Star(context, At(&box, 0.5f, 0.54f), box.width * 0.48f, box.width * 0.2f, color);
        break;
    case UI_ICON_EXIT: {
        const Point door[4] = {At(&box, 0.55f, 0.18f), At(&box, 0.22f, 0.18f),
                               At(&box, 0.22f, 0.82f), At(&box, 0.55f, 0.82f)};
        const Point arrow[3] = {At(&box, 0.70f, 0.34f), At(&box, 0.86f, 0.5f),
                                At(&box, 0.70f, 0.66f)};

        RenderContext_StrokePolyline(context, door, 4, color, stroke, false);
        RenderContext_DrawLine(context, At(&box, 0.42f, 0.5f), At(&box, 0.86f, 0.5f), color,
                               stroke);
        RenderContext_StrokePolyline(context, arrow, 3, color, stroke, false);
        break;
    }
    case UI_ICON_ALERT: {
        Rect ring;
        Rect dot;

        ring.x = box.x + stroke * 0.5f;
        ring.y = box.y + stroke * 0.5f;
        ring.width = box.width - stroke;
        ring.height = box.height - stroke;
        dot.x = box.x + box.width * 0.5f - stroke * 0.6f;
        dot.y = box.y + box.height * 0.72f - stroke * 0.6f;
        dot.width = stroke * 1.2f;
        dot.height = stroke * 1.2f;

        RenderContext_StrokeEllipse(context, &ring, color, stroke * 0.8f);
        RenderContext_DrawLine(context, At(&box, 0.5f, 0.26f), At(&box, 0.5f, 0.58f), color,
                               stroke);
        RenderContext_FillEllipse(context, &dot, color);
        break;
    }
    case UI_ICON_SPARKLE: {
        const Point points[8] = {
            At(&box, 0.5f, 0.0f), At(&box, 0.6f, 0.4f), At(&box, 1.0f, 0.5f), At(&box, 0.6f, 0.6f),
            At(&box, 0.5f, 1.0f), At(&box, 0.4f, 0.6f), At(&box, 0.0f, 0.5f), At(&box, 0.4f, 0.4f)};

        RenderContext_FillPolygon(context, points, 8, color);
        break;
    }
    default:
        break;
    }
}
