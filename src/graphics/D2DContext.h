#pragma once

/*
 * The Direct2D/DirectWrite render context: device resources, transform and opacity stacks,
 * brush and text-format caches, and every draw primitive the UI uses.
 *
 * Pure C.  What changed, and why:
 *   - The five std::map caches and the std::list LRU became fixed arrays with a use stamp.
 *     The C++ version already bounded the gradient cache at 128 entries and the text-format
 *     cache at 64 (then cleared it wholesale); here the caches are 24 entries each and evict
 *     the least recently used entry, which is strictly better behaved than "clear it all" and
 *     keeps the struct a fixed size.
 *   - `std::vector` transform and opacity stacks became fixed arrays with depth counters.
 *     A depth overflow is ignored rather than unbounded, which matches how PopTransform
 *     already refused to pop the identity transform.
 *   - Gradient stop lists arrive as a pointer plus a count instead of std::initializer_list,
 *     and polygons as a pointer plus a count instead of std::span.
 *   - The DWRITE_* enum fields of TextStyle are plain `int` in C, because MSVC's C mode cannot
 *     parse <dwrite.h> at all.  graphics/CppCompat.h keeps the DWRITE-typed C++ TextStyle and
 *     converts at the boundary.
 *   - `std::wstring` in/out became WStr.
 *
 * The PDK_* interface mirrors are only forward declared here and the full shim is included by
 * D2DContext.c: see ProceduralTextures.h for why the shim must stay out of a header that a
 * C++ translation unit includes.
 */

#include "core/Geometry.h"
#include "core/Str.h"
#include "graphics/ProceduralTextures.h"

#include <stdbool.h>
#include <stdint.h>

#include <d2d1.h>
#include <windows.h>
#include <wincodec.h>

typedef struct PDK_ID2D1Factory PDK_ID2D1Factory;
typedef struct PDK_ID2D1RenderTarget PDK_ID2D1RenderTarget;
typedef struct PDK_ID2D1HwndRenderTarget PDK_ID2D1HwndRenderTarget;
typedef struct PDK_ID2D1Brush PDK_ID2D1Brush;
typedef struct PDK_ID2D1SolidColorBrush PDK_ID2D1SolidColorBrush;
typedef struct PDK_ID2D1LinearGradientBrush PDK_ID2D1LinearGradientBrush;
typedef struct PDK_ID2D1RadialGradientBrush PDK_ID2D1RadialGradientBrush;
typedef struct PDK_ID2D1BitmapBrush PDK_ID2D1BitmapBrush;
typedef struct PDK_ID2D1Bitmap PDK_ID2D1Bitmap;
typedef struct PDK_ID2D1GradientStopCollection PDK_ID2D1GradientStopCollection;
typedef struct PDK_ID2D1StrokeStyle PDK_ID2D1StrokeStyle;
typedef struct PDK_ID2D1Geometry PDK_ID2D1Geometry;
typedef struct PDK_ID2D1PathGeometry PDK_ID2D1PathGeometry;
typedef struct PDK_ID2D1GeometrySink PDK_ID2D1GeometrySink;
typedef struct PDK_IDWriteFactory PDK_IDWriteFactory;
typedef struct PDK_IDWriteTextFormat PDK_IDWriteTextFormat;
typedef struct PDK_IDWriteTextLayout PDK_IDWriteTextLayout;
typedef struct PDK_IDWriteInlineObject PDK_IDWriteInlineObject;

#ifdef __cplusplus
extern "C" {
#endif

enum { PDK_GRADIENT_CACHE_MAX = 24 };
enum { PDK_FORMAT_CACHE_MAX = 24 };
enum { PDK_TRANSFORM_DEPTH_MAX = 16 };
enum { PDK_OPACITY_DEPTH_MAX = 16 };
enum { PDK_GRADIENT_STOPS_MAX = 8 };

/* Plain ints, holding the DWRITE_FONT_WEIGHT / DWRITE_TEXT_ALIGNMENT /
 * DWRITE_PARAGRAPH_ALIGNMENT values the shim passes through unchanged. */
enum { FONT_FAMILY_UI = 0 };
enum { FONT_FAMILY_KAI = 1 };

typedef struct TextStyle {
    float size;
    int weight;
    int family;
    int align;
    int valign;
    /* Line height in logical pixels; 0 keeps the font default. */
    float lineHeight;
    bool wrap;
    bool ellipsis;
} TextStyle;

typedef struct GradientStop {
    float position;
    D2D1_COLOR_F color;
} GradientStop;

typedef struct TextFormatEntry {
    uint64_t key;
    PDK_IDWriteTextFormat *format;
    PDK_IDWriteInlineObject *ellipsis;
    uint64_t stamp;
} TextFormatEntry;

/*
 * One gradient key can have a stop collection, a linear brush and a radial brush; the
 * C++ version kept three separate maps plus a shared LRU list, which this collapses into one
 * entry with nullable members.
 */
typedef struct GradientCacheEntry {
    uint64_t key;
    PDK_ID2D1GradientStopCollection *stops;
    PDK_ID2D1LinearGradientBrush *linear;
    PDK_ID2D1RadialGradientBrush *radial;
    uint64_t stamp;
} GradientCacheEntry;

typedef struct RenderContext {
    HWND hwnd;
    int pixelWidth;
    int pixelHeight;
    ViewTransform transform;
    PDK_ID2D1Factory *d2dFactory;
    PDK_IDWriteFactory *dwriteFactory;
    IWICImagingFactory *wicFactory;
    PDK_ID2D1RenderTarget *target;
    PDK_ID2D1HwndRenderTarget *hwndTarget;
    IWICBitmap *offscreen;
    bool offscreenMode;
    PDK_ID2D1StrokeStyle *roundStroke;

    PDK_ID2D1SolidColorBrush *solid;
    GradientCacheEntry gradients[PDK_GRADIENT_CACHE_MAX];
    int gradientCount;
    uint64_t gradientClock;
    TextFormatEntry formats[PDK_FORMAT_CACHE_MAX];
    int formatCount;

    ProceduralTextures textures;

    D2D1_MATRIX_3X2_F transforms[PDK_TRANSFORM_DEPTH_MAX];
    int transformCount;
    float opacities[PDK_OPACITY_DEPTH_MAX];
    int opacityCount;
    float opacity;
} RenderContext;

void RenderContext_Init(RenderContext *context);

/* Offscreen renders into a fixed 1280x720 WIC bitmap instead of the window, so screenshots
 * do not depend on the window being visible on an active display. */
bool RenderContext_Initialize(RenderContext *context, HWND hwnd, bool offscreen);
void RenderContext_Resize(RenderContext *context, int pixelWidth, int pixelHeight);
void RenderContext_BeginFrame(RenderContext *context);
bool RenderContext_EndFrame(RenderContext *context);
void RenderContext_DiscardDeviceResources(RenderContext *context);
bool RenderContext_EnsureDeviceResources(RenderContext *context);
void RenderContext_Shutdown(RenderContext *context);

PDK_ID2D1RenderTarget *RenderContext_Target(const RenderContext *context);
IWICBitmap *RenderContext_OffscreenBitmap(const RenderContext *context);
PDK_ID2D1Factory *RenderContext_Factory(const RenderContext *context);
PDK_IDWriteFactory *RenderContext_DWriteFactory(const RenderContext *context);
IWICImagingFactory *RenderContext_WicFactory(const RenderContext *context);
ViewTransform RenderContext_View(const RenderContext *context);

/* Clear also resets the transform and opacity stacks to the logical 1280x720 view. */
void RenderContext_Clear(RenderContext *context, D2D1_COLOR_F color);

void RenderContext_PushTransform(RenderContext *context, D2D1_MATRIX_3X2_F local);
void RenderContext_PushTranslation(RenderContext *context, float dx, float dy);
void RenderContext_PushScale(RenderContext *context, float scale, Point center);
void RenderContext_PushRotation(RenderContext *context, float degrees, Point center);
void RenderContext_PopTransform(RenderContext *context);
void RenderContext_PushOpacity(RenderContext *context, float opacity);
void RenderContext_PopOpacity(RenderContext *context);
float RenderContext_Opacity(const RenderContext *context);
void RenderContext_PushClip(RenderContext *context, const Rect *rect);
void RenderContext_PopClip(RenderContext *context);

PDK_ID2D1Brush *RenderContext_Solid(RenderContext *context, D2D1_COLOR_F color);
PDK_ID2D1Brush *RenderContext_Linear(RenderContext *context, Point from, Point to,
                                     const GradientStop *stops, int stopCount);
PDK_ID2D1Brush *RenderContext_Radial(RenderContext *context, Point center, float radiusX,
                                     float radiusY, const GradientStop *stops, int stopCount);
/* Tiled felt grain; the caller controls strength with PushOpacity. */
PDK_ID2D1Brush *RenderContext_FeltBrush(RenderContext *context);

void RenderContext_FillRect(RenderContext *context, const Rect *rect, D2D1_COLOR_F color);
void RenderContext_FillRectBrush(RenderContext *context, const Rect *rect, PDK_ID2D1Brush *brush);
void RenderContext_StrokeRect(RenderContext *context, const Rect *rect, D2D1_COLOR_F color,
                              float width);
void RenderContext_FillRoundedRect(RenderContext *context, const Rect *rect, float radius,
                                   D2D1_COLOR_F color);
void RenderContext_FillRoundedRectBrush(RenderContext *context, const Rect *rect, float radius,
                                        PDK_ID2D1Brush *brush);
void RenderContext_StrokeRoundedRect(RenderContext *context, const Rect *rect, float radius,
                                     D2D1_COLOR_F color, float width);
void RenderContext_StrokeRoundedRectBrush(RenderContext *context, const Rect *rect, float radius,
                                          PDK_ID2D1Brush *brush, float width);
void RenderContext_FillEllipse(RenderContext *context, const Rect *rect, D2D1_COLOR_F color);
void RenderContext_FillEllipseBrush(RenderContext *context, const Rect *rect, PDK_ID2D1Brush *brush);
void RenderContext_StrokeEllipse(RenderContext *context, const Rect *rect, D2D1_COLOR_F color,
                                 float width);
void RenderContext_StrokeEllipseBrush(RenderContext *context, const Rect *rect,
                                      PDK_ID2D1Brush *brush, float width);
void RenderContext_DrawLine(RenderContext *context, Point from, Point to, D2D1_COLOR_F color,
                            float width);
void RenderContext_DrawLineBrush(RenderContext *context, Point from, Point to,
                                 PDK_ID2D1Brush *brush, float width);
void RenderContext_FillPolygon(RenderContext *context, const Point *points, int pointCount,
                               D2D1_COLOR_F color);
void RenderContext_StrokePolyline(RenderContext *context, const Point *points, int pointCount,
                                  D2D1_COLOR_F color, float width, bool closed);

/* Soft Gaussian shadow or glow shaped like a rounded rect; blur is the Gaussian sigma in
 * logical pixels. */
void RenderContext_DrawShadow(RenderContext *context, const Rect *rect, float blur,
                              D2D1_COLOR_F color);

void RenderContext_DrawTextUtf8(RenderContext *context, const char *text, const Rect *rect,
                                const TextStyle *style, D2D1_COLOR_F color);
void RenderContext_DrawTextUtf8Brush(RenderContext *context, const char *text, const Rect *rect,
                                     const TextStyle *style, PDK_ID2D1Brush *brush);
void RenderContext_DrawTextUtf8Simple(RenderContext *context, const char *text, const Rect *rect,
                                      float fontSize, D2D1_COLOR_F color, int align, int valign);
void RenderContext_MeasureText(RenderContext *context, const char *text, const TextStyle *style,
                               float maxWidth, Size *out);
/* Layouts give editors caret hit-testing; they do not depend on the device and survive target
 * loss.  Returns NULL on failure. */
PDK_IDWriteTextLayout *RenderContext_CreateTextLayout(RenderContext *context, const wchar_t *text,
                                                      int textLength, const TextStyle *style,
                                                      float maxWidth, float maxHeight);
void RenderContext_DrawTextLayout(RenderContext *context, PDK_IDWriteTextLayout *layout,
                                 Point origin, D2D1_COLOR_F color);

void RenderContext_DrawBitmap(RenderContext *context, PDK_ID2D1Bitmap *bitmap, const Rect *dest,
                              const D2D1_RECT_U *source, float opacity);
void RenderContext_DrawBitmapRect(RenderContext *context, PDK_ID2D1Bitmap *bitmap,
                                  const Rect *dest, const D2D1_RECT_F *source, float opacity);

void RenderContext_Utf8ToWide(const RenderContext *context, const char *text, WStr *out);

static inline GradientStop GradientStop_Make(float position, D2D1_COLOR_F color)
{
    GradientStop stop;

    stop.position = position;
    stop.color = color;
    return stop;
}

static inline TextStyle TextStyle_Default(void)
{
    TextStyle style;
    style.size = 18.0f;
    style.weight = 400;      /* DWRITE_FONT_WEIGHT_NORMAL */
    style.family = FONT_FAMILY_UI;
    style.align = 0;         /* DWRITE_TEXT_ALIGNMENT_LEADING */
    style.valign = 0;        /* DWRITE_PARAGRAPH_ALIGNMENT_NEAR */
    style.lineHeight = 0.0f;
    style.wrap = true;
    style.ellipsis = false;
    return style;
}

/* The C spelling of the C++ facade's Text(size, weight) / Centered(...) / Kai(size). */
static inline TextStyle TextStyle_Label(float size, int weight)
{
    TextStyle style = TextStyle_Default();

    style.size = size;
    style.weight = weight;
    return style;
}

static inline TextStyle TextStyle_Centered(TextStyle style)
{
    style.align = 2;  /* DWRITE_TEXT_ALIGNMENT_CENTER */
    style.valign = 2; /* DWRITE_PARAGRAPH_ALIGNMENT_CENTER */
    return style;
}

static inline TextStyle TextStyle_Kai(float size)
{
    TextStyle style = TextStyle_Default();

    style.size = size;
    style.family = FONT_FAMILY_KAI;
    return style;
}

#ifdef __cplusplus
}
#endif
