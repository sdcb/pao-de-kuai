#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * src/graphics/ProceduralTextures.*, WicImageLoader.* and D2DContext.* are pure C now
 * (plan.md S5): interface pointers are PDK_* mirrors of the SDK types, calls go through
 * PDK_CALL, and the caches and stacks are fixed arrays.
 *
 * The C++ callers still use the old shapes -- a RenderContext object with methods, a ComPtr
 * result from LoadBitmapFromMemory, a DWRITE-typed TextStyle with an enum-class FontFamily --
 * so this header reproduces exactly those on top of the C API, and no call site has to change
 * twice.  The conversions are:
 *   - RenderContext forwards each method; the only real work is ToCTextStyle, which maps the
 *     DWRITE-typed style onto the plain ints the C struct carries (MSVC's C mode cannot parse
 *     <dwrite.h> at all, which is why the C side uses ints there);
 *   - LoadBitmapFromMemory wraps the returned new reference in a ComPtr;
 *   - the interface mirrors are reinterpret_cast'd back to the SDK types, which is a no-op
 *     because shim_layout asserts the layouts are identical at compile time.
 *
 * It grows as the rest of src/graphics is converted and disappears with the last C++ file
 * under src/.
 */

#include "graphics/Com.h"
#include "graphics/ComPtr.h"
#include "graphics/D2DContext.h"
#include "graphics/ProceduralTextures.h"
#include "graphics/WicImageLoader.h"

#include <cstdint>
#include <initializer_list>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <d2d1.h>
#include <dwrite.h>

namespace pdk::graphics {

/* ---- procedural textures --------------------------------------------- */

/* Borrows the caller's bitmap/brush; ownership stays with the ProceduralTextures value. */
inline ID2D1Bitmap* ProceduralShadow(ProceduralTextures& textures, ID2D1RenderTarget* target)
{
    return reinterpret_cast<ID2D1Bitmap*>(
        ProceduralTextures_Shadow(&textures, PDK_AS(ID2D1RenderTarget, target)));
}

inline ID2D1BitmapBrush* ProceduralFelt(ProceduralTextures& textures, ID2D1RenderTarget* target)
{
    return reinterpret_cast<ID2D1BitmapBrush*>(
        ProceduralTextures_Felt(&textures, PDK_AS(ID2D1RenderTarget, target)));
}

/* Accepts anything with data()/size() over bytes, which is what the resource loader hands
 * back.  The returned ComPtr owns the new reference the C function produced. */
template <typename ByteRange>
inline ComPtr<ID2D1Bitmap> LoadBitmapFromMemory(ID2D1RenderTarget* target,
                                               IWICImagingFactory* wicFactory,
                                               const ByteRange& bytes, float scale = 1.0f)
{
    ComPtr<ID2D1Bitmap> bitmap;

    bitmap.Attach(reinterpret_cast<ID2D1Bitmap*>(WicImageLoader_LoadBitmapFromMemory(
        PDK_AS(ID2D1RenderTarget, target), wicFactory,
        reinterpret_cast<const std::uint8_t*>(bytes.data()), static_cast<int>(bytes.size()),
        scale)));
    return bitmap;
}

/* ---- text styles ----------------------------------------------------- */

enum class FontFamily {
    Ui,
    Kai
};

struct TextStyle {
    float size{18.0f};
    DWRITE_FONT_WEIGHT weight{DWRITE_FONT_WEIGHT_NORMAL};
    FontFamily family{FontFamily::Ui};
    DWRITE_TEXT_ALIGNMENT align{DWRITE_TEXT_ALIGNMENT_LEADING};
    DWRITE_PARAGRAPH_ALIGNMENT valign{DWRITE_PARAGRAPH_ALIGNMENT_NEAR};
    // Line height in logical pixels; 0 keeps the font default.
    float lineHeight{0.0f};
    bool wrap{true};
    bool ellipsis{false};
};

using GradientStop = ::GradientStop;

inline ::TextStyle ToCTextStyle(const TextStyle& style)
{
    ::TextStyle out = TextStyle_Default();

    out.size = style.size;
    out.weight = static_cast<int>(style.weight);
    out.family = style.family == FontFamily::Kai ? FONT_FAMILY_KAI : FONT_FAMILY_UI;
    out.align = static_cast<int>(style.align);
    out.valign = static_cast<int>(style.valign);
    out.lineHeight = style.lineHeight;
    out.wrap = style.wrap;
    out.ellipsis = style.ellipsis;
    return out;
}

/* ---- the render context --------------------------------------------- */

class RenderContext {
public:
    RenderContext() { ::RenderContext_Init(&data_); }
    ~RenderContext() { ::RenderContext_Shutdown(&data_); }

    RenderContext(const RenderContext&) = delete;
    RenderContext& operator=(const RenderContext&) = delete;

    // offscreen renders into a fixed 1280x720 WIC bitmap instead of the window, so
    // screenshots do not depend on the window being visible on an active display.
    bool Initialize(HWND hwnd, bool offscreen = false)
    {
        return ::RenderContext_Initialize(&data_, hwnd, offscreen);
    }
    void Resize(int pixelWidth, int pixelHeight)
    {
        ::RenderContext_Resize(&data_, pixelWidth, pixelHeight);
    }
    void BeginFrame() { ::RenderContext_BeginFrame(&data_); }
    bool EndFrame() { return ::RenderContext_EndFrame(&data_); }
    void DiscardDeviceResources() { ::RenderContext_DiscardDeviceResources(&data_); }
    bool EnsureDeviceResources() { return ::RenderContext_EnsureDeviceResources(&data_); }

    ID2D1RenderTarget* Target() const
    {
        return reinterpret_cast<ID2D1RenderTarget*>(::RenderContext_Target(&data_));
    }
    IWICBitmap* OffscreenBitmap() const { return ::RenderContext_OffscreenBitmap(&data_); }
    ID2D1Factory* Factory() const
    {
        return reinterpret_cast<ID2D1Factory*>(::RenderContext_Factory(&data_));
    }
    IDWriteFactory* DWriteFactory() const
    {
        return reinterpret_cast<IDWriteFactory*>(::RenderContext_DWriteFactory(&data_));
    }
    IWICImagingFactory* WicFactory() const { return ::RenderContext_WicFactory(&data_); }
    ViewTransform View() const { return ::RenderContext_View(&data_); }

    // Clear also resets the transform and opacity stacks to the logical 1280x720 view.
    void Clear(D2D1_COLOR_F color) { ::RenderContext_Clear(&data_, color); }

    void PushTransform(const D2D1_MATRIX_3X2_F& local)
    {
        ::RenderContext_PushTransform(&data_, local);
    }
    void PushTranslation(float dx, float dy) { ::RenderContext_PushTranslation(&data_, dx, dy); }
    void PushScale(float scale, Point center)
    {
        ::RenderContext_PushScale(&data_, scale, center);
    }
    void PushRotation(float degrees, Point center)
    {
        ::RenderContext_PushRotation(&data_, degrees, center);
    }
    void PopTransform() { ::RenderContext_PopTransform(&data_); }
    void PushOpacity(float opacity) { ::RenderContext_PushOpacity(&data_, opacity); }
    void PopOpacity() { ::RenderContext_PopOpacity(&data_); }
    float Opacity() const { return ::RenderContext_Opacity(&data_); }
    void PushClip(const Rect& rect) { ::RenderContext_PushClip(&data_, &rect); }
    void PopClip() { ::RenderContext_PopClip(&data_); }

    ID2D1Brush* Solid(D2D1_COLOR_F color)
    {
        return reinterpret_cast<ID2D1Brush*>(::RenderContext_Solid(&data_, color));
    }
    ID2D1Brush* Linear(Point from, Point to, std::initializer_list<GradientStop> stops)
    {
        return reinterpret_cast<ID2D1Brush*>(::RenderContext_Linear(
            &data_, from, to, stops.begin(), static_cast<int>(stops.size())));
    }
    ID2D1Brush* Radial(Point center, float radiusX, float radiusY,
                       std::initializer_list<GradientStop> stops)
    {
        return reinterpret_cast<ID2D1Brush*>(::RenderContext_Radial(
            &data_, center, radiusX, radiusY, stops.begin(), static_cast<int>(stops.size())));
    }
    // Tiled felt grain; the caller controls strength with PushOpacity.
    ID2D1Brush* FeltBrush()
    {
        return reinterpret_cast<ID2D1Brush*>(::RenderContext_FeltBrush(&data_));
    }

    void FillRect(const Rect& rect, D2D1_COLOR_F color)
    {
        ::RenderContext_FillRect(&data_, &rect, color);
    }
    void FillRect(const Rect& rect, ID2D1Brush* brush)
    {
        ::RenderContext_FillRectBrush(&data_, &rect, PDK_AS(ID2D1Brush, brush));
    }
    void StrokeRect(const Rect& rect, D2D1_COLOR_F color, float width = 1.0f)
    {
        ::RenderContext_StrokeRect(&data_, &rect, color, width);
    }
    void FillRoundedRect(const Rect& rect, float radius, D2D1_COLOR_F color)
    {
        ::RenderContext_FillRoundedRect(&data_, &rect, radius, color);
    }
    void FillRoundedRect(const Rect& rect, float radius, ID2D1Brush* brush)
    {
        ::RenderContext_FillRoundedRectBrush(&data_, &rect, radius, PDK_AS(ID2D1Brush, brush));
    }
    void StrokeRoundedRect(const Rect& rect, float radius, D2D1_COLOR_F color, float width = 1.0f)
    {
        ::RenderContext_StrokeRoundedRect(&data_, &rect, radius, color, width);
    }
    void StrokeRoundedRect(const Rect& rect, float radius, ID2D1Brush* brush, float width = 1.0f)
    {
        ::RenderContext_StrokeRoundedRectBrush(&data_, &rect, radius, PDK_AS(ID2D1Brush, brush),
                                               width);
    }
    void FillEllipse(const Rect& rect, D2D1_COLOR_F color)
    {
        ::RenderContext_FillEllipse(&data_, &rect, color);
    }
    void FillEllipse(const Rect& rect, ID2D1Brush* brush)
    {
        ::RenderContext_FillEllipseBrush(&data_, &rect, PDK_AS(ID2D1Brush, brush));
    }
    void StrokeEllipse(const Rect& rect, D2D1_COLOR_F color, float width = 1.0f)
    {
        ::RenderContext_StrokeEllipse(&data_, &rect, color, width);
    }
    void StrokeEllipse(const Rect& rect, ID2D1Brush* brush, float width = 1.0f)
    {
        ::RenderContext_StrokeEllipseBrush(&data_, &rect, PDK_AS(ID2D1Brush, brush), width);
    }
    void DrawLine(Point from, Point to, D2D1_COLOR_F color, float width = 1.0f)
    {
        ::RenderContext_DrawLine(&data_, from, to, color, width);
    }
    void DrawLine(Point from, Point to, ID2D1Brush* brush, float width = 1.0f)
    {
        ::RenderContext_DrawLineBrush(&data_, from, to, PDK_AS(ID2D1Brush, brush), width);
    }
    void FillPolygon(std::span<const Point> points, D2D1_COLOR_F color)
    {
        ::RenderContext_FillPolygon(&data_, points.data(), static_cast<int>(points.size()), color);
    }
    void StrokePolyline(std::span<const Point> points, D2D1_COLOR_F color, float width,
                        bool closed = false)
    {
        ::RenderContext_StrokePolyline(&data_, points.data(), static_cast<int>(points.size()),
                                       color, width, closed);
    }

    // Soft Gaussian shadow or glow shaped like a rounded rect; blur is the Gaussian sigma in
    // logical pixels.
    void DrawShadow(const Rect& rect, float blur, D2D1_COLOR_F color)
    {
        ::RenderContext_DrawShadow(&data_, &rect, blur, color);
    }

    void DrawTextUtf8(const std::string& text, const Rect& rect, const TextStyle& style,
                      D2D1_COLOR_F color)
    {
        const ::TextStyle cstyle = ToCTextStyle(style);

        ::RenderContext_DrawTextUtf8(&data_, text.c_str(), &rect, &cstyle, color);
    }
    void DrawTextUtf8(const std::string& text, const Rect& rect, const TextStyle& style,
                      ID2D1Brush* brush)
    {
        const ::TextStyle cstyle = ToCTextStyle(style);

        ::RenderContext_DrawTextUtf8Brush(&data_, text.c_str(), &rect, &cstyle,
                                          PDK_AS(ID2D1Brush, brush));
    }
    void DrawTextUtf8(const std::string& text, const Rect& rect, float fontSize,
                      D2D1_COLOR_F color,
                      DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
                      DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR)
    {
        ::RenderContext_DrawTextUtf8Simple(&data_, text.c_str(), &rect, fontSize, color,
                                           static_cast<int>(align), static_cast<int>(valign));
    }
    Size MeasureText(const std::string& text, const TextStyle& style, float maxWidth = 4096.0f)
    {
        const ::TextStyle cstyle = ToCTextStyle(style);
        Size size;

        ::RenderContext_MeasureText(&data_, text.c_str(), &cstyle, maxWidth, &size);
        return size;
    }
    // Layouts give editors caret hit-testing; they do not depend on the device and survive
    // target loss.
    ComPtr<IDWriteTextLayout> CreateTextLayout(const std::wstring& text, const TextStyle& style,
                                               float maxWidth, float maxHeight)
    {
        const ::TextStyle cstyle = ToCTextStyle(style);
        ComPtr<IDWriteTextLayout> layout;

        layout.Attach(reinterpret_cast<IDWriteTextLayout*>(::RenderContext_CreateTextLayout(
            &data_, text.c_str(), static_cast<int>(text.size()), &cstyle, maxWidth, maxHeight)));
        return layout;
    }
    void DrawTextLayout(IDWriteTextLayout* layout, Point origin, D2D1_COLOR_F color)
    {
        ::RenderContext_DrawTextLayout(&data_, reinterpret_cast<PDK_IDWriteTextLayout*>(layout),
                                       origin, color);
    }

    void DrawBitmap(ID2D1Bitmap* bitmap, const Rect& dest, const D2D1_RECT_U* source = nullptr,
                    float opacity = 1.0f)
    {
        ::RenderContext_DrawBitmap(&data_, reinterpret_cast<PDK_ID2D1Bitmap*>(bitmap), &dest,
                                   source, opacity);
    }
    void DrawBitmap(ID2D1Bitmap* bitmap, const Rect& dest, const D2D1_RECT_F& source,
                    float opacity = 1.0f)
    {
        ::RenderContext_DrawBitmapRect(&data_, reinterpret_cast<PDK_ID2D1Bitmap*>(bitmap), &dest,
                                       &source, opacity);
    }

    std::wstring Utf8ToWide(const std::string& text) const
    {
        WStr wide;
        std::wstring out;

        WStr_Init(&wide);
        ::RenderContext_Utf8ToWide(&data_, text.c_str(), &wide);
        out.assign(WStr_CStr(&wide));
        WStr_Free(&wide);
        return out;
    }

private:
    ::RenderContext data_;
};

} // namespace pdk::graphics
