#pragma once

#include "core/Geometry.h"
#include "graphics/ComPtr.h"

#include <cstdint>
#include <span>
#include <string>

#include <d2d1.h>
#include <dwrite.h>
#include <windows.h>
#include <wincodec.h>

namespace pdk::graphics {

class RenderContext {
public:
    bool Initialize(HWND hwnd);
    void Resize(int pixelWidth, int pixelHeight);
    void BeginFrame();
    bool EndFrame();
    void DiscardDeviceResources();
    bool EnsureDeviceResources();

    ID2D1HwndRenderTarget* Target() const { return target_.Get(); }
    IDWriteFactory* DWriteFactory() const { return dwriteFactory_.Get(); }
    IWICImagingFactory* WicFactory() const { return wicFactory_.Get(); }
    core::ViewTransform ViewTransform() const { return transform_; }

    void Clear(D2D1_COLOR_F color);
    void FillRect(const core::Rect& rect, D2D1_COLOR_F color);
    void StrokeRect(const core::Rect& rect, D2D1_COLOR_F color, float width = 1.0f);
    void FillEllipse(const core::Rect& rect, D2D1_COLOR_F color);
    void StrokeEllipse(const core::Rect& rect, D2D1_COLOR_F color, float width = 1.0f);
    void DrawTextUtf8(
        const std::string& text,
        const core::Rect& rect,
        float fontSize,
        D2D1_COLOR_F color,
        DWRITE_TEXT_ALIGNMENT align = DWRITE_TEXT_ALIGNMENT_LEADING,
        DWRITE_PARAGRAPH_ALIGNMENT valign = DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    void DrawBitmap(ID2D1Bitmap* bitmap, const core::Rect& dest, const D2D1_RECT_U* source = nullptr, float opacity = 1.0f);

    std::wstring Utf8ToWide(const std::string& text) const;

private:
    bool CreateTarget();

    HWND hwnd_{};
    int pixelWidth_{1280};
    int pixelHeight_{720};
    core::ViewTransform transform_{};
    ComPtr<ID2D1Factory> d2dFactory_;
    ComPtr<IDWriteFactory> dwriteFactory_;
    ComPtr<IWICImagingFactory> wicFactory_;
    ComPtr<ID2D1HwndRenderTarget> target_;
};

} // namespace pdk::graphics
