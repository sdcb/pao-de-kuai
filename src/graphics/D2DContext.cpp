#include "graphics/D2DContext.h"

#include <algorithm>

namespace pdk::graphics {

bool RenderContext::Initialize(HWND hwnd) {
    hwnd_ = hwnd;
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    pixelWidth_ = std::max(1280, static_cast<int>(rc.right - rc.left));
    pixelHeight_ = std::max(720, static_cast<int>(rc.bottom - rc.top));
    transform_ = core::ComputeViewTransform(pixelWidth_, pixelHeight_);

    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, d2dFactory_.ReleaseAndGetAddressOf()))) {
        return false;
    }
    if (FAILED(DWriteCreateFactory(
            DWRITE_FACTORY_TYPE_SHARED,
            __uuidof(IDWriteFactory),
            reinterpret_cast<IUnknown**>(dwriteFactory_.ReleaseAndGetAddressOf())))) {
        return false;
    }
    if (FAILED(CoCreateInstance(
            CLSID_WICImagingFactory,
            nullptr,
            CLSCTX_INPROC_SERVER,
            IID_PPV_ARGS(wicFactory_.ReleaseAndGetAddressOf())))) {
        return false;
    }
    return CreateTarget();
}

bool RenderContext::CreateTarget() {
    if (!d2dFactory_ || !hwnd_) {
        return false;
    }
    RECT rc{};
    GetClientRect(hwnd_, &rc);
    pixelWidth_ = std::max(1280, static_cast<int>(rc.right - rc.left));
    pixelHeight_ = std::max(720, static_cast<int>(rc.bottom - rc.top));
    transform_ = core::ComputeViewTransform(pixelWidth_, pixelHeight_);

    const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
        D2D1_RENDER_TARGET_TYPE_DEFAULT,
        D2D1::PixelFormat(DXGI_FORMAT_UNKNOWN, D2D1_ALPHA_MODE_UNKNOWN),
        96.0f,
        96.0f);
    const D2D1_HWND_RENDER_TARGET_PROPERTIES hwndProps = D2D1::HwndRenderTargetProperties(
        hwnd_,
        D2D1::SizeU(static_cast<UINT32>(pixelWidth_), static_cast<UINT32>(pixelHeight_)),
        D2D1_PRESENT_OPTIONS_NONE);

    return SUCCEEDED(d2dFactory_->CreateHwndRenderTarget(props, hwndProps, target_.ReleaseAndGetAddressOf()));
}

bool RenderContext::EnsureDeviceResources() {
    if (target_) {
        return true;
    }
    return CreateTarget();
}

void RenderContext::DiscardDeviceResources() {
    target_.Reset();
}

void RenderContext::Resize(int pixelWidth, int pixelHeight) {
    pixelWidth_ = std::max(1280, pixelWidth);
    pixelHeight_ = std::max(720, pixelHeight);
    transform_ = core::ComputeViewTransform(pixelWidth_, pixelHeight_);
    if (target_) {
        target_->Resize(D2D1::SizeU(static_cast<UINT32>(pixelWidth_), static_cast<UINT32>(pixelHeight_)));
    }
}

void RenderContext::BeginFrame() {
    EnsureDeviceResources();
    if (!target_) {
        return;
    }
    target_->BeginDraw();
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
}

bool RenderContext::EndFrame() {
    if (!target_) {
        return false;
    }
    const HRESULT hr = target_->EndDraw();
    if (hr == D2DERR_RECREATE_TARGET) {
        DiscardDeviceResources();
        return false;
    }
    return SUCCEEDED(hr);
}

void RenderContext::Clear(D2D1_COLOR_F color) {
    if (!target_) {
        return;
    }
    target_->Clear(color);
    const D2D1_MATRIX_3X2_F matrix = D2D1::Matrix3x2F::Scale(transform_.scale, transform_.scale) *
        D2D1::Matrix3x2F::Translation(transform_.offsetX, transform_.offsetY);
    target_->SetTransform(matrix);
}

void RenderContext::FillRect(const core::Rect& rect, D2D1_COLOR_F color) {
    if (!target_) {
        return;
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(target_->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf()))) {
        target_->FillRectangle(D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height), brush.Get());
    }
}

void RenderContext::StrokeRect(const core::Rect& rect, D2D1_COLOR_F color, float width) {
    if (!target_) {
        return;
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(target_->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf()))) {
        target_->DrawRectangle(D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height), brush.Get(), width);
    }
}

void RenderContext::FillEllipse(const core::Rect& rect, D2D1_COLOR_F color) {
    if (!target_) {
        return;
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(target_->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf()))) {
        target_->FillEllipse(
            D2D1::Ellipse(
                D2D1::Point2F(rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f),
                rect.width * 0.5f,
                rect.height * 0.5f),
            brush.Get());
    }
}

void RenderContext::StrokeEllipse(const core::Rect& rect, D2D1_COLOR_F color, float width) {
    if (!target_) {
        return;
    }
    ComPtr<ID2D1SolidColorBrush> brush;
    if (SUCCEEDED(target_->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf()))) {
        target_->DrawEllipse(
            D2D1::Ellipse(
                D2D1::Point2F(rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f),
                rect.width * 0.5f,
                rect.height * 0.5f),
            brush.Get(),
            width);
    }
}

void RenderContext::DrawTextUtf8(
    const std::string& text,
    const core::Rect& rect,
    float fontSize,
    D2D1_COLOR_F color,
    DWRITE_TEXT_ALIGNMENT align,
    DWRITE_PARAGRAPH_ALIGNMENT valign) {
    if (!target_ || !dwriteFactory_) {
        return;
    }
    ComPtr<IDWriteTextFormat> format;
    if (FAILED(dwriteFactory_->CreateTextFormat(
            L"Microsoft YaHei UI",
            nullptr,
            DWRITE_FONT_WEIGHT_NORMAL,
            DWRITE_FONT_STYLE_NORMAL,
            DWRITE_FONT_STRETCH_NORMAL,
            fontSize,
            L"zh-CN",
            format.ReleaseAndGetAddressOf()))) {
        return;
    }
    format->SetTextAlignment(align);
    format->SetParagraphAlignment(valign);
    format->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);

    ComPtr<ID2D1SolidColorBrush> brush;
    if (FAILED(target_->CreateSolidColorBrush(color, brush.ReleaseAndGetAddressOf()))) {
        return;
    }
    const std::wstring wide = Utf8ToWide(text);
    target_->DrawTextW(
        wide.c_str(),
        static_cast<UINT32>(wide.size()),
        format.Get(),
        D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height),
        brush.Get());
}

void RenderContext::DrawBitmap(ID2D1Bitmap* bitmap, const core::Rect& dest, const D2D1_RECT_U* source, float opacity) {
    if (!target_ || !bitmap) {
        return;
    }
    const D2D1_RECT_F destRect = D2D1::RectF(dest.x, dest.y, dest.x + dest.width, dest.y + dest.height);
    D2D1_RECT_F sourceRect{};
    const D2D1_RECT_F* sourcePtr = nullptr;
    if (source) {
        sourceRect = D2D1::RectF(
            static_cast<float>(source->left),
            static_cast<float>(source->top),
            static_cast<float>(source->right),
            static_cast<float>(source->bottom));
        sourcePtr = &sourceRect;
    }
    target_->DrawBitmap(bitmap, destRect, opacity, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, sourcePtr);
}

std::wstring RenderContext::Utf8ToWide(const std::string& text) const {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    if (size <= 0) {
        return std::wstring(text.begin(), text.end());
    }
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

} // namespace pdk::graphics
