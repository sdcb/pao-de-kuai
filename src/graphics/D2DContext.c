#include "graphics/win_compat.h"

#include "graphics/d2d_c.h"
#include "graphics/D2DContext.h"

#include <wincodec.h>

#include <math.h>
#include <string.h>

/* ---- D2D1::* helpers, filled in directly ------------------------------ */

static D2D1_MATRIX_3X2_F MatrixIdentity(void)
{
    D2D1_MATRIX_3X2_F m;
    m._11 = 1.0f;
    m._12 = 0.0f;
    m._21 = 0.0f;
    m._22 = 1.0f;
    m._31 = 0.0f;
    m._32 = 0.0f;
    return m;
}

static D2D1_MATRIX_3X2_F MatrixTranslation(float dx, float dy)
{
    D2D1_MATRIX_3X2_F m = MatrixIdentity();
    m._31 = dx;
    m._32 = dy;
    return m;
}

static D2D1_MATRIX_3X2_F MatrixScale(float xScale, float yScale)
{
    D2D1_MATRIX_3X2_F m = MatrixIdentity();
    m._11 = xScale;
    m._22 = yScale;
    return m;
}

static D2D1_MATRIX_3X2_F MatrixScaleAt(float xScale, float yScale, D2D1_POINT_2F center)
{
    D2D1_MATRIX_3X2_F m = MatrixScale(xScale, yScale);
    m._31 = center.x - center.x * xScale;
    m._32 = center.y - center.y * yScale;
    return m;
}

static D2D1_MATRIX_3X2_F MatrixRotation(float degrees, D2D1_POINT_2F center)
{
    const float radians = degrees * 3.14159265358979323846f / 180.0f;
    const float sinAngle = sinf(radians);
    const float cosAngle = cosf(radians);
    D2D1_MATRIX_3X2_F m;

    m._11 = cosAngle;
    m._12 = sinAngle;
    m._21 = -sinAngle;
    m._22 = cosAngle;
    m._31 = center.x - center.x * cosAngle + center.y * sinAngle;
    m._32 = center.y - center.x * sinAngle - center.y * cosAngle;
    return m;
}

/* Same as D2D1Matrix3x2FMultiply, i.e. Matrix3x2F::operator* : row vectors, A then B. */
static D2D1_MATRIX_3X2_F MatrixMultiply(D2D1_MATRIX_3X2_F a, D2D1_MATRIX_3X2_F b)
{
    D2D1_MATRIX_3X2_F r;

    r._11 = a._11 * b._11 + a._12 * b._21;
    r._12 = a._11 * b._12 + a._12 * b._22;
    r._21 = a._21 * b._11 + a._22 * b._21;
    r._22 = a._21 * b._12 + a._22 * b._22;
    r._31 = a._31 * b._11 + a._32 * b._21 + b._31;
    r._32 = a._31 * b._12 + a._32 * b._22 + b._32;
    return r;
}

static D2D1_POINT_2F Point2F(float x, float y)
{
    D2D1_POINT_2F p;
    p.x = x;
    p.y = y;
    return p;
}

static D2D1_RECT_F ToRectF(const Rect *rect)
{
    D2D1_RECT_F r;
    r.left = rect->x;
    r.top = rect->y;
    r.right = rect->x + rect->width;
    r.bottom = rect->y + rect->height;
    return r;
}

static D2D1_ROUNDED_RECT ToRounded(const Rect *rect, float radius)
{
    float r = radius;
    const float half = (rect->width < rect->height ? rect->width : rect->height) * 0.5f;

    if (r < 0.0f) {
        r = 0.0f;
    }
    if (r > half) {
        r = half;
    }
    {
        D2D1_ROUNDED_RECT rounded;
        rounded.rect = ToRectF(rect);
        rounded.radiusX = r;
        rounded.radiusY = r;
        return rounded;
    }
}

static D2D1_ELLIPSE ToEllipse(const Rect *rect)
{
    D2D1_ELLIPSE e;
    e.point = Point2F(rect->x + rect->width * 0.5f, rect->y + rect->height * 0.5f);
    e.radiusX = rect->width * 0.5f;
    e.radiusY = rect->height * 0.5f;
    return e;
}

static D2D1_PIXEL_FORMAT PixelFormat(DXGI_FORMAT format, D2D1_ALPHA_MODE alphaMode)
{
    D2D1_PIXEL_FORMAT p;
    p.format = format;
    p.alphaMode = alphaMode;
    return p;
}

static D2D1_RENDER_TARGET_PROPERTIES RenderTargetProperties(D2D1_RENDER_TARGET_TYPE type,
                                                           D2D1_PIXEL_FORMAT pixelFormat)
{
    D2D1_RENDER_TARGET_PROPERTIES props;
    props.type = type;
    props.pixelFormat = pixelFormat;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    props.usage = D2D1_RENDER_TARGET_USAGE_NONE;
    props.minLevel = D2D1_FEATURE_LEVEL_DEFAULT;
    return props;
}

static D2D1_SIZE_U SizeU32(UINT32 width, UINT32 height)
{
    D2D1_SIZE_U s;
    s.width = width;
    s.height = height;
    return s;
}

static D2D1_STROKE_STYLE_PROPERTIES RoundStrokeProperties(void)
{
    D2D1_STROKE_STYLE_PROPERTIES props;
    props.startCap = D2D1_CAP_STYLE_ROUND;
    props.endCap = D2D1_CAP_STYLE_ROUND;
    props.dashCap = D2D1_CAP_STYLE_ROUND;
    props.lineJoin = D2D1_LINE_JOIN_ROUND;
    props.miterLimit = 10.0f;
    props.dashStyle = D2D1_DASH_STYLE_SOLID;
    props.dashOffset = 0.0f;
    return props;
}

/* The flat shim mirrors the COM signatures, i.e. these take pointers, while the C++
 * interface's inline wrappers took references. */
static void SetTargetTransform(RenderContext *context, D2D1_MATRIX_3X2_F transform)
{
    PDK_CALL(context->target, SetTransform, &transform);
}

/* Every brush-derived interface declares SetOpacity with a PDK_ID2D1Brush This. */
static void BrushSetOpacity(void *brush, float opacity)
{
    PDK_CALL((PDK_ID2D1Brush *)brush, SetOpacity, opacity);
}

static const wchar_t *FamilyName(int family)
{
    return family == FONT_FAMILY_KAI ? L"KaiTi" : L"Microsoft YaHei UI";
}

static void HashBytes(uint64_t *hash, const void *data, size_t size)
{
    const unsigned char *bytes = (const unsigned char *)data;

    for (size_t i = 0; i < size; ++i) {
        *hash ^= bytes[i];
        *hash *= 1099511628211ull;
    }
}

static int MaxInt(int a, int b)
{
    return a > b ? a : b;
}

/* ---- lifetime --------------------------------------------------------- */

void RenderContext_Init(RenderContext *context)
{
    memset(context, 0, sizeof(*context));
    context->pixelWidth = 1280;
    context->pixelHeight = 720;
    context->transform = ViewTransform_Identity();
    context->opacity = 1.0f;
    ProceduralTextures_Init(&context->textures);
}

static void ClearGradientCache(RenderContext *context)
{
    for (int i = 0; i < context->gradientCount; ++i) {
        PDK_RELEASE(context->gradients[i].stops);
        PDK_RELEASE(context->gradients[i].linear);
        PDK_RELEASE(context->gradients[i].radial);
    }
    context->gradientCount = 0;
    context->gradientClock = 0;
}

void RenderContext_DiscardDeviceResources(RenderContext *context)
{
    PDK_RELEASE(context->solid);
    ClearGradientCache(context);
    ProceduralTextures_Reset(&context->textures);
    PDK_RELEASE(context->target);
    PDK_RELEASE(context->hwndTarget);
}

void RenderContext_Shutdown(RenderContext *context)
{
    RenderContext_DiscardDeviceResources(context);
    for (int i = 0; i < context->formatCount; ++i) {
        PDK_RELEASE(context->formats[i].format);
        PDK_RELEASE(context->formats[i].ellipsis);
    }
    context->formatCount = 0;
    PDK_RELEASE(context->roundStroke);
    PDK_RELEASE(context->offscreen);
    PDK_RELEASE(context->wicFactory);
    PDK_RELEASE(context->dwriteFactory);
    PDK_RELEASE(context->d2dFactory);
}

static bool CreateTarget(RenderContext *context)
{
    if (context->d2dFactory == NULL || context->hwnd == NULL) {
        return false;
    }
    if (context->offscreenMode) {
        D2D1_RENDER_TARGET_PROPERTIES props;

        context->pixelWidth = 1280;
        context->pixelHeight = 720;
        context->transform = ComputeViewTransform(context->pixelWidth, context->pixelHeight);
        if (context->wicFactory == NULL ||
            FAILED(PDK_CALL(context->wicFactory, CreateBitmap, (UINT)context->pixelWidth,
                            (UINT)context->pixelHeight, &GUID_WICPixelFormat32bppPBGRA,
                            WICBitmapCacheOnLoad, &context->offscreen))) {
            return false;
        }
        props = RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
        if (FAILED(PDK_CALL(context->d2dFactory, CreateWicBitmapRenderTarget, context->offscreen,
                            &props, &context->target))) {
            return false;
        }
    } else {
        RECT rc;
        D2D1_RENDER_TARGET_PROPERTIES props;
        D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps;

        GetClientRect(context->hwnd, &rc);
        context->pixelWidth = MaxInt(1280, (int)(rc.right - rc.left));
        context->pixelHeight = MaxInt(720, (int)(rc.bottom - rc.top));
        context->transform = ComputeViewTransform(context->pixelWidth, context->pixelHeight);

        props = RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN));
        hwndProps.hwnd = context->hwnd;
        hwndProps.pixelSize =
            SizeU32((UINT32)context->pixelWidth, (UINT32)context->pixelHeight);
        hwndProps.presentOptions = D2D1_PRESENT_OPTIONS_NONE;

        if (FAILED(PDK_CALL(context->d2dFactory, CreateHwndRenderTarget, &props, &hwndProps,
                            &context->hwndTarget))) {
            return false;
        }
        /* Borrows the hwnd target: two owners of one reference, as before. */
        context->target = (PDK_ID2D1RenderTarget *)context->hwndTarget;
        PDK_ADDREF(context->target);
    }
    /* Grayscale AA keeps text clean on coloured, translucent and rotated surfaces. */
    PDK_CALL(context->target, SetTextAntialiasMode, D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

bool RenderContext_EnsureDeviceResources(RenderContext *context)
{
    if (context->target != NULL) {
        return true;
    }
    return CreateTarget(context);
}

bool RenderContext_Initialize(RenderContext *context, HWND hwnd, bool offscreen)
{
    RECT rc;

    context->hwnd = hwnd;
    context->offscreenMode = offscreen;
    GetClientRect(hwnd, &rc);
    context->pixelWidth = MaxInt(1280, (int)(rc.right - rc.left));
    context->pixelHeight = MaxInt(720, (int)(rc.bottom - rc.top));
    context->transform = ComputeViewTransform(context->pixelWidth, context->pixelHeight);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &IID_ID2D1Factory, NULL,
                                 (void **)&context->d2dFactory))) {
        return false;
    }
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, &IID_IDWriteFactory,
                                   (PDK_IUnknown **)&context->dwriteFactory))) {
        return false;
    }
    if (FAILED(CoCreateInstance(&CLSID_WICImagingFactory, NULL, CLSCTX_INPROC_SERVER,
                                &IID_IWICImagingFactory, (void **)&context->wicFactory))) {
        return false;
    }
    return CreateTarget(context);
}

void RenderContext_Resize(RenderContext *context, int pixelWidth, int pixelHeight)
{
    if (context->offscreenMode) {
        return;
    }
    context->pixelWidth = MaxInt(1280, pixelWidth);
    context->pixelHeight = MaxInt(720, pixelHeight);
    context->transform = ComputeViewTransform(context->pixelWidth, context->pixelHeight);
    if (context->hwndTarget != NULL) {
        D2D1_SIZE_U size;

        size = SizeU32((UINT32)context->pixelWidth, (UINT32)context->pixelHeight);
        PDK_CALL(context->hwndTarget, Resize, &size);
    }
}

void RenderContext_BeginFrame(RenderContext *context)
{
    RenderContext_EnsureDeviceResources(context);
    if (context->target == NULL) {
        return;
    }
    PDK_CALL0(context->target, BeginDraw);
    SetTargetTransform(context, MatrixIdentity());
}

bool RenderContext_EndFrame(RenderContext *context)
{
    HRESULT hr;

    if (context->target == NULL) {
        return false;
    }
    hr = PDK_CALL(context->target, EndDraw, NULL, NULL);
    if (hr == D2DERR_RECREATE_TARGET) {
        RenderContext_DiscardDeviceResources(context);
        return false;
    }
    return SUCCEEDED(hr);
}

PDK_ID2D1RenderTarget *RenderContext_Target(const RenderContext *context)
{
    return context->target;
}

IWICBitmap *RenderContext_OffscreenBitmap(const RenderContext *context)
{
    return context->offscreen;
}

PDK_ID2D1Factory *RenderContext_Factory(const RenderContext *context)
{
    return context->d2dFactory;
}

PDK_IDWriteFactory *RenderContext_DWriteFactory(const RenderContext *context)
{
    return context->dwriteFactory;
}

IWICImagingFactory *RenderContext_WicFactory(const RenderContext *context)
{
    return context->wicFactory;
}

ViewTransform RenderContext_View(const RenderContext *context)
{
    return context->transform;
}

/* ---- stacks ----------------------------------------------------------- */

static void ApplyTransform(RenderContext *context)
{
    if (context->target != NULL && context->transformCount > 0) {
        SetTargetTransform(context, context->transforms[context->transformCount - 1]);
    }
}

void RenderContext_Clear(RenderContext *context, D2D1_COLOR_F color)
{
    if (context->target == NULL) {
        return;
    }
    SetTargetTransform(context, MatrixIdentity());
    PDK_CALL(context->target, Clear, &color);

    context->transformCount = 0;
    context->transforms[context->transformCount++] = MatrixMultiply(
        MatrixScale(context->transform.scale, context->transform.scale),
        MatrixTranslation(context->transform.offsetX, context->transform.offsetY));
    context->opacityCount = 0;
    context->opacity = 1.0f;
    ApplyTransform(context);
}

void RenderContext_PushTransform(RenderContext *context, D2D1_MATRIX_3X2_F local)
{
    D2D1_MATRIX_3X2_F parent;

    if (context->transformCount >= PDK_TRANSFORM_DEPTH_MAX) {
        return;
    }
    parent = context->transformCount > 0 ? context->transforms[context->transformCount - 1]
                                        : MatrixIdentity();
    context->transforms[context->transformCount++] = MatrixMultiply(local, parent);
    ApplyTransform(context);
}

void RenderContext_PushTranslation(RenderContext *context, float dx, float dy)
{
    RenderContext_PushTransform(context, MatrixTranslation(dx, dy));
}

void RenderContext_PushScale(RenderContext *context, float scale, Point center)
{
    RenderContext_PushTransform(context, MatrixScaleAt(scale, scale, Point2F(center.x, center.y)));
}

void RenderContext_PushRotation(RenderContext *context, float degrees, Point center)
{
    RenderContext_PushTransform(context, MatrixRotation(degrees, Point2F(center.x, center.y)));
}

void RenderContext_PopTransform(RenderContext *context)
{
    if (context->transformCount > 1) {
        context->transformCount--;
    }
    ApplyTransform(context);
}

void RenderContext_PushOpacity(RenderContext *context, float opacity)
{
    if (context->opacityCount >= PDK_OPACITY_DEPTH_MAX) {
        return;
    }
    context->opacities[context->opacityCount++] = context->opacity;
    if (opacity < 0.0f) {
        opacity = 0.0f;
    } else if (opacity > 1.0f) {
        opacity = 1.0f;
    }
    context->opacity *= opacity;
}

void RenderContext_PopOpacity(RenderContext *context)
{
    if (context->opacityCount > 0) {
        context->opacity = context->opacities[--context->opacityCount];
    }
}

float RenderContext_Opacity(const RenderContext *context)
{
    return context->opacity;
}

void RenderContext_PushClip(RenderContext *context, const Rect *rect)
{
    if (context->target != NULL) {
        const D2D1_RECT_F clip = ToRectF(rect);

        PDK_CALL(context->target, PushAxisAlignedClip, &clip,
                 D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
}

void RenderContext_PopClip(RenderContext *context)
{
    if (context->target != NULL) {
        PDK_CALL0(context->target, PopAxisAlignedClip);
    }
}

/* ---- brushes ---------------------------------------------------------- */

PDK_ID2D1Brush *RenderContext_Solid(RenderContext *context, D2D1_COLOR_F color)
{
    if (context->target == NULL) {
        return NULL;
    }
    if (context->solid == NULL &&
        FAILED(PDK_CALL(context->target, CreateSolidColorBrush, &color, NULL,
                        &context->solid))) {
        return NULL;
    }
    PDK_CALL(context->solid, SetColor, &color);
    BrushSetOpacity(context->solid, context->opacity);
    return (PDK_ID2D1Brush *)context->solid;
}

/* Evicts the least recently used gradient key, dropping all three of its resources. */
static void EvictOldestGradient(RenderContext *context)
{
    int oldest = 0;

    for (int i = 1; i < context->gradientCount; ++i) {
        if (context->gradients[i].stamp < context->gradients[oldest].stamp) {
            oldest = i;
        }
    }
    PDK_RELEASE(context->gradients[oldest].stops);
    PDK_RELEASE(context->gradients[oldest].linear);
    PDK_RELEASE(context->gradients[oldest].radial);
    for (int i = oldest; i + 1 < context->gradientCount; ++i) {
        context->gradients[i] = context->gradients[i + 1];
    }
    context->gradientCount--;
}

static GradientCacheEntry *FindGradient(RenderContext *context, uint64_t key)
{
    for (int i = 0; i < context->gradientCount; ++i) {
        if (context->gradients[i].key == key) {
            context->gradients[i].stamp = ++context->gradientClock;
            return &context->gradients[i];
        }
    }
    return NULL;
}

static GradientCacheEntry *InsertGradient(RenderContext *context, uint64_t key)
{
    GradientCacheEntry *entry;

    if (context->gradientCount >= PDK_GRADIENT_CACHE_MAX) {
        EvictOldestGradient(context);
    }
    entry = &context->gradients[context->gradientCount++];
    entry->key = key;
    entry->stops = NULL;
    entry->linear = NULL;
    entry->radial = NULL;
    entry->stamp = ++context->gradientClock;
    return entry;
}

static PDK_ID2D1GradientStopCollection *Stops(RenderContext *context, const GradientStop *stops,
                                              int stopCount, uint64_t *outKey)
{
    uint64_t key = 1469598103934665603ull;
    GradientCacheEntry *entry;
    D2D1_GRADIENT_STOP d2dStops[PDK_GRADIENT_STOPS_MAX];
    int count;

    for (int i = 0; i < stopCount; ++i) {
        HashBytes(&key, &stops[i].position, sizeof(stops[i].position));
        HashBytes(&key, &stops[i].color, sizeof(stops[i].color));
    }
    *outKey = key;

    entry = FindGradient(context, key);
    if (entry != NULL) {
        return entry->stops;
    }
    entry = InsertGradient(context, key);
    count = stopCount < PDK_GRADIENT_STOPS_MAX ? stopCount : PDK_GRADIENT_STOPS_MAX;
    for (int i = 0; i < count; ++i) {
        d2dStops[i].position = stops[i].position;
        d2dStops[i].color = stops[i].color;
    }
    if (FAILED(PDK_CALL(context->target, CreateGradientStopCollection, d2dStops, (UINT32)count,
                        D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &entry->stops))) {
        return NULL;
    }
    return entry->stops;
}

PDK_ID2D1Brush *RenderContext_Linear(RenderContext *context, Point from, Point to,
                                     const GradientStop *stops, int stopCount)
{
    uint64_t key = 0;
    PDK_ID2D1GradientStopCollection *collection;
    GradientCacheEntry *entry;

    if (context->target == NULL) {
        return NULL;
    }
    collection = Stops(context, stops, stopCount, &key);
    if (collection == NULL) {
        return NULL;
    }
    entry = FindGradient(context, key);
    if (entry == NULL) {
        return NULL;
    }
    if (entry->linear == NULL) {
        D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES props;

        props.startPoint = Point2F(from.x, from.y);
        props.endPoint = Point2F(to.x, to.y);
        if (FAILED(PDK_CALL(context->target, CreateLinearGradientBrush, &props, NULL, collection,
                            &entry->linear))) {
            return NULL;
        }
    }
    PDK_CALL(entry->linear, SetStartPoint, Point2F(from.x, from.y));
    PDK_CALL(entry->linear, SetEndPoint, Point2F(to.x, to.y));
    BrushSetOpacity(entry->linear, context->opacity);
    return (PDK_ID2D1Brush *)entry->linear;
}

PDK_ID2D1Brush *RenderContext_Radial(RenderContext *context, Point center, float radiusX,
                                     float radiusY, const GradientStop *stops, int stopCount)
{
    uint64_t key = 0;
    PDK_ID2D1GradientStopCollection *collection;
    GradientCacheEntry *entry;

    if (context->target == NULL) {
        return NULL;
    }
    collection = Stops(context, stops, stopCount, &key);
    if (collection == NULL) {
        return NULL;
    }
    entry = FindGradient(context, key);
    if (entry == NULL) {
        return NULL;
    }
    if (entry->radial == NULL) {
        D2D1_RADIAL_GRADIENT_BRUSH_PROPERTIES props;

        props.center = Point2F(center.x, center.y);
        props.gradientOriginOffset = Point2F(0.0f, 0.0f);
        props.radiusX = radiusX;
        props.radiusY = radiusY;
        if (FAILED(PDK_CALL(context->target, CreateRadialGradientBrush, &props, NULL, collection,
                            &entry->radial))) {
            return NULL;
        }
    }
    PDK_CALL(entry->radial, SetCenter, Point2F(center.x, center.y));
    PDK_CALL(entry->radial, SetGradientOriginOffset, Point2F(0.0f, 0.0f));
    PDK_CALL(entry->radial, SetRadiusX, radiusX);
    PDK_CALL(entry->radial, SetRadiusY, radiusY);
    BrushSetOpacity(entry->radial, context->opacity);
    return (PDK_ID2D1Brush *)entry->radial;
}

PDK_ID2D1Brush *RenderContext_FeltBrush(RenderContext *context)
{
    PDK_ID2D1BitmapBrush *brush = ProceduralTextures_Felt(&context->textures, context->target);

    if (brush != NULL) {
        BrushSetOpacity(brush, context->opacity);
    }
    return (PDK_ID2D1Brush *)brush;
}

static PDK_ID2D1StrokeStyle *RoundStroke(RenderContext *context)
{
    if (context->roundStroke == NULL && context->d2dFactory != NULL) {
        const D2D1_STROKE_STYLE_PROPERTIES props = RoundStrokeProperties();

        PDK_CALL(context->d2dFactory, CreateStrokeStyle, &props, NULL, 0,
                 &context->roundStroke);
    }
    return context->roundStroke;
}

/* ---- primitives ------------------------------------------------------- */

void RenderContext_FillRect(RenderContext *context, const Rect *rect, D2D1_COLOR_F color)
{
    RenderContext_FillRectBrush(context, rect, RenderContext_Solid(context, color));
}

void RenderContext_FillRectBrush(RenderContext *context, const Rect *rect, PDK_ID2D1Brush *brush)
{
    if (context->target != NULL && brush != NULL) {
        const D2D1_RECT_F area = ToRectF(rect);

        PDK_CALL(context->target, FillRectangle, &area, brush);
    }
}

void RenderContext_StrokeRect(RenderContext *context, const Rect *rect, D2D1_COLOR_F color,
                              float width)
{
    PDK_ID2D1Brush *brush = RenderContext_Solid(context, color);

    if (brush != NULL) {
        const D2D1_RECT_F area = ToRectF(rect);

        PDK_CALL(context->target, DrawRectangle, &area, brush, width,
                 RoundStroke(context));
    }
}

void RenderContext_FillRoundedRect(RenderContext *context, const Rect *rect, float radius,
                                   D2D1_COLOR_F color)
{
    RenderContext_FillRoundedRectBrush(context, rect, radius, RenderContext_Solid(context, color));
}

void RenderContext_FillRoundedRectBrush(RenderContext *context, const Rect *rect, float radius,
                                        PDK_ID2D1Brush *brush)
{
    if (context->target != NULL && brush != NULL) {
        const D2D1_ROUNDED_RECT area = ToRounded(rect, radius);

        PDK_CALL(context->target, FillRoundedRectangle, &area, brush);
    }
}

void RenderContext_StrokeRoundedRect(RenderContext *context, const Rect *rect, float radius,
                                     D2D1_COLOR_F color, float width)
{
    RenderContext_StrokeRoundedRectBrush(context, rect, radius, RenderContext_Solid(context, color),
                                         width);
}

void RenderContext_StrokeRoundedRectBrush(RenderContext *context, const Rect *rect, float radius,
                                          PDK_ID2D1Brush *brush, float width)
{
    if (context->target != NULL && brush != NULL) {
        const D2D1_ROUNDED_RECT area = ToRounded(rect, radius);

        PDK_CALL(context->target, DrawRoundedRectangle, &area, brush, width,
                 RoundStroke(context));
    }
}

void RenderContext_FillEllipse(RenderContext *context, const Rect *rect, D2D1_COLOR_F color)
{
    RenderContext_FillEllipseBrush(context, rect, RenderContext_Solid(context, color));
}

void RenderContext_FillEllipseBrush(RenderContext *context, const Rect *rect,
                                    PDK_ID2D1Brush *brush)
{
    if (context->target != NULL && brush != NULL) {
        const D2D1_ELLIPSE area = ToEllipse(rect);

        PDK_CALL(context->target, FillEllipse, &area, brush);
    }
}

void RenderContext_StrokeEllipse(RenderContext *context, const Rect *rect, D2D1_COLOR_F color,
                                 float width)
{
    RenderContext_StrokeEllipseBrush(context, rect, RenderContext_Solid(context, color), width);
}

void RenderContext_StrokeEllipseBrush(RenderContext *context, const Rect *rect,
                                      PDK_ID2D1Brush *brush, float width)
{
    if (context->target != NULL && brush != NULL) {
        const D2D1_ELLIPSE area = ToEllipse(rect);

        PDK_CALL(context->target, DrawEllipse, &area, brush, width,
                 RoundStroke(context));
    }
}

void RenderContext_DrawLine(RenderContext *context, Point from, Point to, D2D1_COLOR_F color,
                            float width)
{
    RenderContext_DrawLineBrush(context, from, to, RenderContext_Solid(context, color), width);
}

void RenderContext_DrawLineBrush(RenderContext *context, Point from, Point to,
                                 PDK_ID2D1Brush *brush, float width)
{
    if (context->target != NULL && brush != NULL) {
        PDK_CALL(context->target, DrawLine, Point2F(from.x, from.y), Point2F(to.x, to.y), brush,
                 width, RoundStroke(context));
    }
}

void RenderContext_FillPolygon(RenderContext *context, const Point *points, int pointCount,
                               D2D1_COLOR_F color)
{
    PDK_ID2D1PathGeometry *path = NULL;
    PDK_ID2D1GeometrySink *sink = NULL;
    PDK_ID2D1Brush *brush;

    if (context->target == NULL || context->d2dFactory == NULL || pointCount < 3) {
        return;
    }
    if (FAILED(PDK_CALL(context->d2dFactory, CreatePathGeometry, &path)) ||
        FAILED(PDK_CALL(path, Open, &sink))) {
        PDK_RELEASE(sink);
        PDK_RELEASE(path);
        return;
    }
    PDK_CALL(sink, BeginFigure, Point2F(points[0].x, points[0].y), D2D1_FIGURE_BEGIN_FILLED);
    for (int i = 1; i < pointCount; ++i) {
        const D2D1_POINT_2F vertex = Point2F(points[i].x, points[i].y);

        PDK_CALL(sink, AddLines, &vertex, 1);
    }
    PDK_CALL(sink, EndFigure, D2D1_FIGURE_END_CLOSED);
    PDK_CALL0(sink, Close);
    PDK_RELEASE(sink);

    brush = RenderContext_Solid(context, color);
    if (brush != NULL) {
        PDK_CALL(context->target, FillGeometry, (PDK_ID2D1Geometry *)path, brush, NULL);
    }
    PDK_RELEASE(path);
}

void RenderContext_StrokePolyline(RenderContext *context, const Point *points, int pointCount,
                                  D2D1_COLOR_F color, float width, bool closed)
{
    PDK_ID2D1PathGeometry *path = NULL;
    PDK_ID2D1GeometrySink *sink = NULL;
    PDK_ID2D1Brush *brush;

    if (context->target == NULL || context->d2dFactory == NULL || pointCount < 2) {
        return;
    }
    if (FAILED(PDK_CALL(context->d2dFactory, CreatePathGeometry, &path)) ||
        FAILED(PDK_CALL(path, Open, &sink))) {
        PDK_RELEASE(sink);
        PDK_RELEASE(path);
        return;
    }
    PDK_CALL(sink, BeginFigure, Point2F(points[0].x, points[0].y), D2D1_FIGURE_BEGIN_HOLLOW);
    for (int i = 1; i < pointCount; ++i) {
        const D2D1_POINT_2F vertex = Point2F(points[i].x, points[i].y);

        PDK_CALL(sink, AddLines, &vertex, 1);
    }
    PDK_CALL(sink, EndFigure, closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    PDK_CALL0(sink, Close);
    PDK_RELEASE(sink);

    brush = RenderContext_Solid(context, color);
    if (brush != NULL) {
        PDK_CALL(context->target, DrawGeometry, (PDK_ID2D1Geometry *)path, brush, width,
                 RoundStroke(context));
    }
    PDK_RELEASE(path);
}

void RenderContext_DrawShadow(RenderContext *context, const Rect *rect, float blur,
                              D2D1_COLOR_F color)
{
    const float size = (float)PDK_SHADOW_SIZE;
    /* Corner slices stop just short of the centre where the blurred mask is flat. */
    const float corner = size * 0.5f - 4.0f;
    PDK_ID2D1Bitmap *mask;
    PDK_ID2D1Brush *brush;
    Rect outer;
    float k;
    float pad;
    float cornerW;
    float cornerH;
    float dx[4];
    float dy[4];
    float sx[4];
    float sy[4];

    if (context->target == NULL || blur <= 0.0f || context->opacity <= 0.0f) {
        return;
    }
    mask = ProceduralTextures_Shadow(&context->textures, context->target);
    brush = RenderContext_Solid(context, color);
    if (mask == NULL || brush == NULL) {
        return;
    }
    k = blur / PDK_SHADOW_SIGMA;
    pad = (float)PDK_SHADOW_PAD * k;
    outer.x = rect->x - pad;
    outer.y = rect->y - pad;
    outer.width = rect->width + pad * 2.0f;
    outer.height = rect->height + pad * 2.0f;
    cornerW = corner * k < outer.width * 0.5f ? corner * k : outer.width * 0.5f;
    cornerH = corner * k < outer.height * 0.5f ? corner * k : outer.height * 0.5f;
    dx[0] = outer.x;
    dx[1] = outer.x + cornerW;
    dx[2] = outer.x + outer.width - cornerW;
    dx[3] = outer.x + outer.width;
    dy[0] = outer.y;
    dy[1] = outer.y + cornerH;
    dy[2] = outer.y + outer.height - cornerH;
    dy[3] = outer.y + outer.height;
    sx[0] = 0.0f;
    sx[1] = cornerW / k;
    sx[2] = size - cornerW / k;
    sx[3] = size;
    sy[0] = 0.0f;
    sy[1] = cornerH / k;
    sy[2] = size - cornerH / k;
    sy[3] = size;

    /* Aliased mode makes adjacent slices share edges exactly, so no seams appear. */
    PDK_CALL(context->target, SetAntialiasMode, D2D1_ANTIALIAS_MODE_ALIASED);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            D2D1_RECT_F dest;
            D2D1_RECT_F src;

            if (dx[col + 1] - dx[col] <= 0.0f || dy[row + 1] - dy[row] <= 0.0f) {
                continue;
            }
            dest.left = dx[col];
            dest.top = dy[row];
            dest.right = dx[col + 1];
            dest.bottom = dy[row + 1];
            src.left = sx[col];
            src.top = sy[row];
            src.right = sx[col] + 0.5f > sx[col + 1] ? sx[col] + 0.5f : sx[col + 1];
            src.bottom = sy[row] + 0.5f > sy[row + 1] ? sy[row] + 0.5f : sy[row + 1];
            PDK_CALL(context->target, FillOpacityMask, mask, brush,
                     D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &dest, &src);
        }
    }
    PDK_CALL(context->target, SetAntialiasMode, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

/* ---- text ------------------------------------------------------------- */

static TextFormatEntry *Format(RenderContext *context, const TextStyle *style)
{
    uint64_t key;
    TextFormatEntry *entry = NULL;
    PDK_IDWriteTextFormat *format;
    DWRITE_TRIMMING trimming;

    if (context->dwriteFactory == NULL) {
        return NULL;
    }
    key = ((uint64_t)style->family << 40) | ((uint64_t)style->weight << 24) |
          (uint64_t)(style->size * 16.0f);

    for (int i = 0; i < context->formatCount; ++i) {
        if (context->formats[i].key == key) {
            entry = &context->formats[i];
            break;
        }
    }
    if (entry == NULL) {
        int oldest = 0;

        if (context->formatCount >= PDK_FORMAT_CACHE_MAX) {
            /* The C++ version cleared the whole cache here; evicting the least recently used
             * entry keeps the same bound without discarding the hot formats. */
            for (int i = 1; i < context->formatCount; ++i) {
                if (context->formats[i].stamp < context->formats[oldest].stamp) {
                    oldest = i;
                }
            }
            PDK_RELEASE(context->formats[oldest].format);
            PDK_RELEASE(context->formats[oldest].ellipsis);
            for (int i = oldest; i + 1 < context->formatCount; ++i) {
                context->formats[i] = context->formats[i + 1];
            }
            context->formatCount--;
        }
        entry = &context->formats[context->formatCount];
        entry->key = key;
        entry->format = NULL;
        entry->ellipsis = NULL;
        if (FAILED(PDK_CALL(context->dwriteFactory, CreateTextFormat,
                            FamilyName(style->family), NULL, (DWRITE_FONT_WEIGHT)style->weight,
                            DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, style->size,
                            L"zh-CN", &entry->format))) {
            return NULL;
        }
        PDK_CALL(context->dwriteFactory, CreateEllipsisTrimmingSign, entry->format,
                 &entry->ellipsis);
        entry->stamp = ++context->gradientClock;
        context->formatCount++;
    }

    format = entry->format;
    PDK_CALL(format, SetTextAlignment, (DWRITE_TEXT_ALIGNMENT)style->align);
    PDK_CALL(format, SetParagraphAlignment, (DWRITE_PARAGRAPH_ALIGNMENT)style->valign);
    PDK_CALL(format, SetWordWrapping,
             style->wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (style->lineHeight > 0.0f) {
        PDK_CALL(format, SetLineSpacing, DWRITE_LINE_SPACING_METHOD_UNIFORM, style->lineHeight,
                 style->lineHeight * 0.78f);
    } else {
        PDK_CALL(format, SetLineSpacing, DWRITE_LINE_SPACING_METHOD_DEFAULT, 0.0f, 0.0f);
    }
    trimming.granularity = style->ellipsis ? DWRITE_TRIMMING_GRANULARITY_CHARACTER
                                          : DWRITE_TRIMMING_GRANULARITY_NONE;
    trimming.delimiter = 0;
    trimming.delimiterCount = 0;
    PDK_CALL(format, SetTrimming, &trimming,
             style->ellipsis ? entry->ellipsis : NULL);
    return entry;
}

void RenderContext_Utf8ToWide(const RenderContext *context, const char *text, WStr *out)
{
    const int length = (int)strlen(text);
    int size;

    PDK_UNUSED(context);
    WStr_Clear(out);
    if (length == 0) {
        return;
    }
    size = MultiByteToWideChar(CP_UTF8, 0, text, length, NULL, 0);
    if (size <= 0) {
        /* Not valid UTF-8: fall back to one wide char per byte, as the C++ version did. */
        for (int i = 0; i < length; ++i) {
            WStr_AppendChar(out, (wchar_t)(unsigned char)text[i]);
        }
        return;
    }
    if (!WStr_Reserve(out, size) || out->data == NULL) {
        return;
    }
    MultiByteToWideChar(CP_UTF8, 0, text, length, out->data, size);
    out->data[size] = L'\0';
    out->len = size;
}

void RenderContext_DrawTextUtf8(RenderContext *context, const char *text, const Rect *rect,
                                const TextStyle *style, D2D1_COLOR_F color)
{
    RenderContext_DrawTextUtf8Brush(context, text, rect, style,
                                    RenderContext_Solid(context, color));
}

void RenderContext_DrawTextUtf8Brush(RenderContext *context, const char *text, const Rect *rect,
                                     const TextStyle *style, PDK_ID2D1Brush *brush)
{
    TextFormatEntry *entry;
    WStr wide;

    if (context->target == NULL || brush == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    entry = Format(context, style);
    if (entry == NULL) {
        return;
    }
    WStr_Init(&wide);
    RenderContext_Utf8ToWide(context, text, &wide);
    {
        const D2D1_RECT_F layoutRect = ToRectF(rect);

        PDK_CALL(context->target, DrawText, WStr_CStr(&wide), (UINT32)wide.len, entry->format,
                 &layoutRect, brush, D2D1_DRAW_TEXT_OPTIONS_NONE,
                 DWRITE_MEASURING_MODE_NATURAL);
    }
    WStr_Free(&wide);
}

void RenderContext_DrawTextUtf8Simple(RenderContext *context, const char *text, const Rect *rect,
                                      float fontSize, D2D1_COLOR_F color, int align, int valign)
{
    TextStyle style = TextStyle_Default();

    style.size = fontSize;
    style.align = align;
    style.valign = valign;
    RenderContext_DrawTextUtf8(context, text, rect, &style, color);
}

void RenderContext_MeasureText(RenderContext *context, const char *text, const TextStyle *style,
                               float maxWidth, Size *out)
{
    TextFormatEntry *entry;
    WStr wide;
    PDK_IDWriteTextLayout *layout = NULL;
    DWRITE_TEXT_METRICS metrics;

    out->width = 0.0f;
    out->height = 0.0f;
    entry = Format(context, style);
    if (entry == NULL || text == NULL || text[0] == '\0') {
        return;
    }
    WStr_Init(&wide);
    RenderContext_Utf8ToWide(context, text, &wide);
    if (SUCCEEDED(PDK_CALL(context->dwriteFactory, CreateTextLayout, WStr_CStr(&wide),
                           (UINT32)wide.len, entry->format, maxWidth, 4096.0f, &layout))) {
        PDK_CALL(layout, GetMetrics, &metrics);
        out->width = metrics.widthIncludingTrailingWhitespace;
        out->height = metrics.height;
        PDK_RELEASE(layout);
    }
    WStr_Free(&wide);
}

PDK_IDWriteTextLayout *RenderContext_CreateTextLayout(RenderContext *context, const wchar_t *text,
                                                      int textLength, const TextStyle *style,
                                                      float maxWidth, float maxHeight)
{
    TextFormatEntry *entry = Format(context, style);
    PDK_IDWriteTextLayout *layout = NULL;

    if (entry == NULL) {
        return NULL;
    }
    PDK_CALL(context->dwriteFactory, CreateTextLayout, text, (UINT32)textLength, entry->format,
             maxWidth, maxHeight, &layout);
    return layout;
}

void RenderContext_DrawTextLayout(RenderContext *context, PDK_IDWriteTextLayout *layout,
                                 Point origin, D2D1_COLOR_F color)
{
    PDK_ID2D1Brush *brush = RenderContext_Solid(context, color);

    if (context->target == NULL || layout == NULL || brush == NULL) {
        return;
    }
    PDK_CALL(context->target, DrawTextLayout, Point2F(origin.x, origin.y), layout, brush,
             D2D1_DRAW_TEXT_OPTIONS_NONE);
}

/* ---- bitmaps ---------------------------------------------------------- */

void RenderContext_DrawBitmap(RenderContext *context, PDK_ID2D1Bitmap *bitmap, const Rect *dest,
                              const D2D1_RECT_U *source, float opacity)
{
    if (context->target == NULL || bitmap == NULL) {
        return;
    }
    if (source != NULL) {
        D2D1_RECT_F sourceRect;

        sourceRect.left = (float)source->left;
        sourceRect.top = (float)source->top;
        sourceRect.right = (float)source->right;
        sourceRect.bottom = (float)source->bottom;
        RenderContext_DrawBitmapRect(context, bitmap, dest, &sourceRect, opacity);
        return;
    }
    {
        const D2D1_RECT_F destination = ToRectF(dest);

        PDK_CALL(context->target, DrawBitmap, bitmap, &destination,
                 opacity * context->opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, NULL);
    }
}

void RenderContext_DrawBitmapRect(RenderContext *context, PDK_ID2D1Bitmap *bitmap,
                                  const Rect *dest, const D2D1_RECT_F *source, float opacity)
{
    if (context->target == NULL || bitmap == NULL) {
        return;
    }
    {
        const D2D1_RECT_F destination = ToRectF(dest);

        PDK_CALL(context->target, DrawBitmap, bitmap, &destination,
                 opacity * context->opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, source);
    }
}
