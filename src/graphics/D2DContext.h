#pragma once

#include "core/Geometry.h"
#include "graphics/ComPtr.h"
#include "graphics/ProceduralTextures.h"

#include <cstdint>
#include <initializer_list>
#include <list>
#include <map>
#include <span>
#include <string>
#include <vector>

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wincodec.h>

namespace pdk::graphics {

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

struct GradientStop {
    float position;
    D2D1_COLOR_F color;
};

class RenderContext {
public:
    // offscreen renders into a fixed 1280x720 WIC bitmap instead of the window, so
    // screenshots do not depend on the window being visible on an active display.
    bool Initialize(HWND hwnd, bool offscreen = false);
    void Resize(int pixelWidth, int pixelHeight);
    void BeginFrame();
    bool EndFrame();
    void DiscardDeviceResources();
    bool EnsureDeviceResources();

    ID2D1RenderTarget* Target() const { return target_.Get(); }
    IWICBitmap* OffscreenBitmap() const { return offscreen_.Get(); }
    ID2D1Factory* Factory() const { return d2dFactory_.Get(); }
    IDWriteFactory* DWriteFactory() const { return dwriteFactory_.Get(); }
    IWICImagingFactory* WicFactory() const { return wicFactory_.Get(); }
    ViewTransform View() const { return transform_; }

    // Clear also resets the transform and opacity stacks to the logical 1280x720 view.
    void Clear(D2D1_COLOR_F color);

    void PushTransform(const D2D1_MATRIX_3X2_F& local);
    void PushTranslation(float dx, float dy);
    void PushScale(float scale, Point center);
    void PushRotation(float degrees, Point center);
    void PopTransform();
    void PushOpacity(float opacity);
    void PopOpacity();
    float Opacity() const { return opacity_; }
    void PushClip(const Rect& rect);
    void PopClip();

    ID2D1Brush* Solid(D2D1_COLOR_F color);
    ID2D1Brush* Linear(Point from, Point to, std::initializer_list<GradientStop> stops);
    ID2D1Brush* Radial(Point center, float radiusX, float radiusY, std::initializer_list<GradientStop> stops);
    // Tiled felt grain; the caller controls strength with PushOpacity.
    ID2D1Brush* FeltBrush();

    void FillRect(const Rect& rect, D2D1_COLOR_F color);
    void FillRect(const Rect& rect, ID2D1Brush* brush);
    void StrokeRect(const Rect& rect, D2D1_COLOR_F color, float width = 1.0f);
    void FillRoundedRect(const Rect& rect, float radius, D2D1_COLOR_F color);
    void FillRoundedRect(const Rect& rect, float radius, ID2D1Brush* brush);
    void StrokeRoundedRect(const Rect& rect, float radius, D2D1_COLOR_F color, float width = 1.0f);
    void StrokeRoundedRect(const Rect& rect, float radius, ID2D1Brush* brush, float width = 1.0f);
    void FillEllipse(const Rect& rect, D2D1_COLOR_F color);
    void FillEllipse(const Rect& rect, ID2D1Brush* brush);
    void StrokeEllipse(const Rect& rect, D2D1_COLOR_F color, float width = 1.0f);
    void StrokeEllipse(const Rect& rect, ID2D1Brush* brush, float width = 1.0f);
    void DrawLine(Point from, Point to, D2D1_COLOR_F color, float width = 1.0f);
    void DrawLine(Point from, Point to, ID2D1Brush* brush, float width = 1.0f);
    void FillPolygon(std::span<const Point> points, D2D1_COLOR_F color);
    void StrokePolyline(std::span<const Point> points, D2D1_COLOR_F color, float width, bool closed = false);

    // Soft Gaussian shadow or glow shaped like a rounded rect; blur is the Gaussian sigma in logical pixels.
    void DrawShadow(const Rect& rect, float blur, D2D1_COLOR_F color);

    void DrawTextUtf8(const std::string& text, const Rect& rect, const TextStyle& style, D2D1_COLOR_F color);
    void DrawTextUtf8(const std::string& text, const Rect& rect, const TextStyle& style, ID2D1Brush* brush);
    void DrawTextUtf8(
        const std::string& text,
        const Rect& rect,
        float fontSize,
        D2D1_COLOR_F color,
        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    Size MeasureText(const std::string& text, const TextStyle& style, float maxWidth = 4096.0f);
    // Layouts give editors caret hit-testing; they do not depend on the device and survive target loss.
    ComPtr<IDWriteTextLayout> CreateTextLayout(const std::wstring& text, const TextStyle& style, float maxWidth, float maxHeight);
    void DrawTextLayout(IDWriteTextLayout* layout, Point origin, D2D1_COLOR_F color);

    void DrawBitmap(ID2D1Bitmap* bitmap, const Rect& dest, const D2D1_RECT_U* source = nullptr, float opacity = 1.0f);
    void DrawBitmap(ID2D1Bitmap* bitmap, const Rect& dest, const D2D1_RECT_F& source, float opacity = 1.0f);

    std::wstring Utf8ToWide(const std::string& text) const;

private:
    struct TextFormatEntry {
        ComPtr<IDWriteTextFormat> format;
        ComPtr<IDWriteInlineObject> ellipsis;
    };

    bool CreateTarget();
    void ApplyTransform();
    ID2D1GradientStopCollection* Stops(std::initializer_list<GradientStop> stops, std::uint64_t& key);
    TextFormatEntry* Format(const TextStyle& style);
    ID2D1StrokeStyle* RoundStroke();

    HWND hwnd_{};
    int pixelWidth_{1280};
    int pixelHeight_{720};
    /* ViewTransform has no default member initialiser any more (C has
     * none), so seed the scale explicitly; ComputeViewTransform overwrites it
     * in Initialize(). */
    ViewTransform transform_ = ViewTransform_Identity();
    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;
    ComPtr<ID2D1RenderTarget> target_;
    ComPtr<ID2D1HwndRenderTarget> hwndTarget_;
    ComPtr<IWICBitmap> offscreen_;
    bool offscreenMode_{false};
    ComPtr<ID2D1StrokeStyle> roundStroke_;

    ComPtr<ID2D1SolidColorBrush> solid_;
    // All gradient caches share the stops-hash key space; gradientOrder_ tracks
    // least-recently-used order so animated gradients cannot grow them forever.
    std::map<std::uint64_t, ComPtr<ID2D1GradientStopCollection>> stops_;
    std::map<std::uint64_t, ComPtr<ID2D1LinearGradientBrush>> linear_;
    std::map<std::uint64_t, ComPtr<ID2D1RadialGradientBrush>> radial_;
    std::list<std::uint64_t> gradientOrder_;
    std::map<std::uint64_t, std::list<std::uint64_t>::iterator> gradientLookup_;
    std::map<std::uint64_t, TextFormatEntry> formats_;
    ProceduralTextures textures_;

    std::vector<D2D1_MATRIX_3X2_F> transforms_;
    std::vector<float> opacities_;
    float opacity_{1.0f};
};

} // namespace pdk::graphics
