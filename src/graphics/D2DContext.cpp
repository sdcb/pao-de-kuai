#include "graphics/D2DContext.h"

#include <algorithm>

namespace pdk::graphics {
namespace {

D2D1_RECT_F ToRectF(const core::Rect& rect) {
    return D2D1::RectF(rect.x, rect.y, rect.x + rect.width, rect.y + rect.height);
}

D2D1_ROUNDED_RECT ToRounded(const core::Rect& rect, float radius) {
    const float r = std::max(0.0f, std::min(radius, std::min(rect.width, rect.height) * 0.5f));
    return D2D1::RoundedRect(ToRectF(rect), r, r);
}

D2D1_ELLIPSE ToEllipse(const core::Rect& rect) {
    return D2D1::Ellipse(
        D2D1::Point2F(rect.x + rect.width * 0.5f, rect.y + rect.height * 0.5f),
        rect.width * 0.5f,
        rect.height * 0.5f);
}

void HashBytes(std::uint64_t& hash, const void* data, std::size_t size) {
    const auto* bytes = static_cast<const unsigned char*>(data);
    for (std::size_t i = 0; i < size; ++i) {
        hash ^= bytes[i];
        hash *= 1099511628211ull;
    }
}

const wchar_t* FamilyName(FontFamily family) {
    return family == FontFamily::Kai ? L"KaiTi" : L"Microsoft YaHei UI";
}

} // namespace

bool RenderContext::Initialize(HWND hwnd, bool offscreen) {
    hwnd_ = hwnd;
    offscreenMode_ = offscreen;
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
    if (offscreenMode_) {
        pixelWidth_ = 1280;
        pixelHeight_ = 720;
        transform_ = core::ComputeViewTransform(pixelWidth_, pixelHeight_);
        if (!wicFactory_ || FAILED(wicFactory_->CreateBitmap(
                static_cast<UINT>(pixelWidth_), static_cast<UINT>(pixelHeight_), GUID_WICPixelFormat32bppPBGRA,
                WICBitmapCacheOnLoad, offscreen_.ReleaseAndGetAddressOf()))) {
            return false;
        }
        const D2D1_RENDER_TARGET_PROPERTIES props = D2D1::RenderTargetProperties(
            D2D1_RENDER_TARGET_TYPE_DEFAULT,
            D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED),
            96.0f,
            96.0f);
        if (FAILED(d2dFactory_->CreateWicBitmapRenderTarget(offscreen_.Get(), props, target_.ReleaseAndGetAddressOf()))) {
            return false;
        }
    } else {
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

        if (FAILED(d2dFactory_->CreateHwndRenderTarget(props, hwndProps, hwndTarget_.ReleaseAndGetAddressOf()))) {
            return false;
        }
        target_.Attach(hwndTarget_.Get());
        target_->AddRef();
    }
    // Grayscale AA keeps text clean on coloured, translucent and rotated surfaces.
    target_->SetTextAntialiasMode(D2D1_TEXT_ANTIALIAS_MODE_GRAYSCALE);
    return true;
}

bool RenderContext::EnsureDeviceResources() {
    if (target_) {
        return true;
    }
    return CreateTarget();
}

void RenderContext::DiscardDeviceResources() {
    solid_.Reset();
    linear_.clear();
    radial_.clear();
    stops_.clear();
    textures_.Reset();
    target_.Reset();
    hwndTarget_.Reset();
}

void RenderContext::Resize(int pixelWidth, int pixelHeight) {
    if (offscreenMode_) {
        return;
    }
    pixelWidth_ = std::max(1280, pixelWidth);
    pixelHeight_ = std::max(720, pixelHeight);
    transform_ = core::ComputeViewTransform(pixelWidth_, pixelHeight_);
    if (hwndTarget_) {
        hwndTarget_->Resize(D2D1::SizeU(static_cast<UINT32>(pixelWidth_), static_cast<UINT32>(pixelHeight_)));
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
    target_->SetTransform(D2D1::Matrix3x2F::Identity());
    target_->Clear(color);
    transforms_.clear();
    transforms_.push_back(D2D1::Matrix3x2F::Scale(transform_.scale, transform_.scale) *
        D2D1::Matrix3x2F::Translation(transform_.offsetX, transform_.offsetY));
    opacities_.clear();
    opacity_ = 1.0f;
    ApplyTransform();
}

void RenderContext::ApplyTransform() {
    if (target_ && !transforms_.empty()) {
        target_->SetTransform(transforms_.back());
    }
}

void RenderContext::PushTransform(const D2D1_MATRIX_3X2_F& local) {
    D2D1_MATRIX_3X2_F child = local;
    D2D1_MATRIX_3X2_F parent = transforms_.empty() ? D2D1::Matrix3x2F::Identity() : transforms_.back();
    transforms_.push_back(*D2D1::Matrix3x2F::ReinterpretBaseType(&child) * *D2D1::Matrix3x2F::ReinterpretBaseType(&parent));
    ApplyTransform();
}

void RenderContext::PushTranslation(float dx, float dy) {
    PushTransform(D2D1::Matrix3x2F::Translation(dx, dy));
}

void RenderContext::PushScale(float scale, core::Point center) {
    PushTransform(D2D1::Matrix3x2F::Scale(scale, scale, D2D1::Point2F(center.x, center.y)));
}

void RenderContext::PushRotation(float degrees, core::Point center) {
    PushTransform(D2D1::Matrix3x2F::Rotation(degrees, D2D1::Point2F(center.x, center.y)));
}

void RenderContext::PopTransform() {
    if (transforms_.size() > 1) {
        transforms_.pop_back();
    }
    ApplyTransform();
}

void RenderContext::PushOpacity(float opacity) {
    opacities_.push_back(opacity_);
    opacity_ *= std::clamp(opacity, 0.0f, 1.0f);
}

void RenderContext::PopOpacity() {
    if (!opacities_.empty()) {
        opacity_ = opacities_.back();
        opacities_.pop_back();
    }
}

void RenderContext::PushClip(const core::Rect& rect) {
    if (target_) {
        target_->PushAxisAlignedClip(ToRectF(rect), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    }
}

void RenderContext::PopClip() {
    if (target_) {
        target_->PopAxisAlignedClip();
    }
}

ID2D1Brush* RenderContext::Solid(D2D1_COLOR_F color) {
    if (!target_) {
        return nullptr;
    }
    if (!solid_ && FAILED(target_->CreateSolidColorBrush(color, solid_.ReleaseAndGetAddressOf()))) {
        return nullptr;
    }
    solid_->SetColor(color);
    solid_->SetOpacity(opacity_);
    return solid_.Get();
}

ID2D1GradientStopCollection* RenderContext::Stops(std::initializer_list<GradientStop> stops, std::uint64_t& key) {
    key = 1469598103934665603ull;
    for (const GradientStop& stop : stops) {
        HashBytes(key, &stop.position, sizeof(stop.position));
        HashBytes(key, &stop.color, sizeof(stop.color));
    }
    auto it = stops_.find(key);
    if (it != stops_.end()) {
        return it->second.Get();
    }
    std::vector<D2D1_GRADIENT_STOP> d2dStops;
    d2dStops.reserve(stops.size());
    for (const GradientStop& stop : stops) {
        d2dStops.push_back(D2D1::GradientStop(stop.position, stop.color));
    }
    ComPtr<ID2D1GradientStopCollection> collection;
    if (FAILED(target_->CreateGradientStopCollection(
            d2dStops.data(),
            static_cast<UINT32>(d2dStops.size()),
            D2D1_GAMMA_2_2,
            D2D1_EXTEND_MODE_CLAMP,
            collection.ReleaseAndGetAddressOf()))) {
        return nullptr;
    }
    ID2D1GradientStopCollection* raw = collection.Get();
    stops_.emplace(key, std::move(collection));
    return raw;
}

ID2D1Brush* RenderContext::Linear(core::Point from, core::Point to, std::initializer_list<GradientStop> stops) {
    if (!target_) {
        return nullptr;
    }
    std::uint64_t key = 0;
    ID2D1GradientStopCollection* collection = Stops(stops, key);
    if (!collection) {
        return nullptr;
    }
    auto it = linear_.find(key);
    if (it == linear_.end()) {
        ComPtr<ID2D1LinearGradientBrush> brush;
        if (FAILED(target_->CreateLinearGradientBrush(
                D2D1::LinearGradientBrushProperties(D2D1::Point2F(from.x, from.y), D2D1::Point2F(to.x, to.y)),
                collection,
                brush.ReleaseAndGetAddressOf()))) {
            return nullptr;
        }
        it = linear_.emplace(key, std::move(brush)).first;
    }
    it->second->SetStartPoint(D2D1::Point2F(from.x, from.y));
    it->second->SetEndPoint(D2D1::Point2F(to.x, to.y));
    it->second->SetOpacity(opacity_);
    return it->second.Get();
}

ID2D1Brush* RenderContext::Radial(core::Point center, float radiusX, float radiusY, std::initializer_list<GradientStop> stops) {
    if (!target_) {
        return nullptr;
    }
    std::uint64_t key = 0;
    ID2D1GradientStopCollection* collection = Stops(stops, key);
    if (!collection) {
        return nullptr;
    }
    auto it = radial_.find(key);
    if (it == radial_.end()) {
        ComPtr<ID2D1RadialGradientBrush> brush;
        if (FAILED(target_->CreateRadialGradientBrush(
                D2D1::RadialGradientBrushProperties(D2D1::Point2F(center.x, center.y), D2D1::Point2F(0.0f, 0.0f), radiusX, radiusY),
                collection,
                brush.ReleaseAndGetAddressOf()))) {
            return nullptr;
        }
        it = radial_.emplace(key, std::move(brush)).first;
    }
    it->second->SetCenter(D2D1::Point2F(center.x, center.y));
    it->second->SetGradientOriginOffset(D2D1::Point2F(0.0f, 0.0f));
    it->second->SetRadiusX(radiusX);
    it->second->SetRadiusY(radiusY);
    it->second->SetOpacity(opacity_);
    return it->second.Get();
}

ID2D1Brush* RenderContext::FeltBrush() {
    ID2D1BitmapBrush* brush = textures_.Felt(target_.Get());
    if (brush) {
        brush->SetOpacity(opacity_);
    }
    return brush;
}

void RenderContext::FillRect(const core::Rect& rect, D2D1_COLOR_F color) {
    FillRect(rect, Solid(color));
}

void RenderContext::FillRect(const core::Rect& rect, ID2D1Brush* brush) {
    if (target_ && brush) {
        target_->FillRectangle(ToRectF(rect), brush);
    }
}

void RenderContext::StrokeRect(const core::Rect& rect, D2D1_COLOR_F color, float width) {
    if (ID2D1Brush* brush = Solid(color)) {
        target_->DrawRectangle(ToRectF(rect), brush, width);
    }
}

void RenderContext::FillRoundedRect(const core::Rect& rect, float radius, D2D1_COLOR_F color) {
    FillRoundedRect(rect, radius, Solid(color));
}

void RenderContext::FillRoundedRect(const core::Rect& rect, float radius, ID2D1Brush* brush) {
    if (target_ && brush) {
        target_->FillRoundedRectangle(ToRounded(rect, radius), brush);
    }
}

void RenderContext::StrokeRoundedRect(const core::Rect& rect, float radius, D2D1_COLOR_F color, float width) {
    StrokeRoundedRect(rect, radius, Solid(color), width);
}

void RenderContext::StrokeRoundedRect(const core::Rect& rect, float radius, ID2D1Brush* brush, float width) {
    if (target_ && brush) {
        target_->DrawRoundedRectangle(ToRounded(rect, radius), brush, width);
    }
}

void RenderContext::FillEllipse(const core::Rect& rect, D2D1_COLOR_F color) {
    FillEllipse(rect, Solid(color));
}

void RenderContext::FillEllipse(const core::Rect& rect, ID2D1Brush* brush) {
    if (target_ && brush) {
        target_->FillEllipse(ToEllipse(rect), brush);
    }
}

void RenderContext::StrokeEllipse(const core::Rect& rect, D2D1_COLOR_F color, float width) {
    StrokeEllipse(rect, Solid(color), width);
}

void RenderContext::StrokeEllipse(const core::Rect& rect, ID2D1Brush* brush, float width) {
    if (target_ && brush) {
        target_->DrawEllipse(ToEllipse(rect), brush, width);
    }
}

void RenderContext::DrawLine(core::Point from, core::Point to, D2D1_COLOR_F color, float width) {
    DrawLine(from, to, Solid(color), width);
}

void RenderContext::DrawLine(core::Point from, core::Point to, ID2D1Brush* brush, float width) {
    if (target_ && brush) {
        target_->DrawLine(D2D1::Point2F(from.x, from.y), D2D1::Point2F(to.x, to.y), brush, width, RoundStroke());
    }
}

ID2D1StrokeStyle* RenderContext::RoundStroke() {
    if (!roundStroke_ && d2dFactory_) {
        const D2D1_STROKE_STYLE_PROPERTIES props = D2D1::StrokeStyleProperties(
            D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_CAP_STYLE_ROUND, D2D1_LINE_JOIN_ROUND);
        d2dFactory_->CreateStrokeStyle(props, nullptr, 0, roundStroke_.ReleaseAndGetAddressOf());
    }
    return roundStroke_.Get();
}

void RenderContext::FillPolygon(std::span<const core::Point> points, D2D1_COLOR_F color) {
    if (!target_ || !d2dFactory_ || points.size() < 3) {
        return;
    }
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(d2dFactory_->CreatePathGeometry(path.ReleaseAndGetAddressOf())) || FAILED(path->Open(sink.ReleaseAndGetAddressOf()))) {
        return;
    }
    sink->BeginFigure(D2D1::Point2F(points[0].x, points[0].y), D2D1_FIGURE_BEGIN_FILLED);
    for (std::size_t i = 1; i < points.size(); ++i) {
        sink->AddLine(D2D1::Point2F(points[i].x, points[i].y));
    }
    sink->EndFigure(D2D1_FIGURE_END_CLOSED);
    sink->Close();
    if (ID2D1Brush* brush = Solid(color)) {
        target_->FillGeometry(path.Get(), brush);
    }
}

void RenderContext::StrokePolyline(std::span<const core::Point> points, D2D1_COLOR_F color, float width, bool closed) {
    if (!target_ || !d2dFactory_ || points.size() < 2) {
        return;
    }
    ComPtr<ID2D1PathGeometry> path;
    ComPtr<ID2D1GeometrySink> sink;
    if (FAILED(d2dFactory_->CreatePathGeometry(path.ReleaseAndGetAddressOf())) || FAILED(path->Open(sink.ReleaseAndGetAddressOf()))) {
        return;
    }
    sink->BeginFigure(D2D1::Point2F(points[0].x, points[0].y), D2D1_FIGURE_BEGIN_HOLLOW);
    for (std::size_t i = 1; i < points.size(); ++i) {
        sink->AddLine(D2D1::Point2F(points[i].x, points[i].y));
    }
    sink->EndFigure(closed ? D2D1_FIGURE_END_CLOSED : D2D1_FIGURE_END_OPEN);
    sink->Close();
    if (ID2D1Brush* brush = Solid(color)) {
        target_->DrawGeometry(path.Get(), brush, width, RoundStroke());
    }
}

void RenderContext::DrawShadow(const core::Rect& rect, float blur, D2D1_COLOR_F color) {
    if (!target_ || blur <= 0.0f || opacity_ <= 0.0f) {
        return;
    }
    ID2D1Bitmap* mask = textures_.Shadow(target_.Get());
    ID2D1Brush* brush = Solid(color);
    if (!mask || !brush) {
        return;
    }
    constexpr float size = static_cast<float>(ProceduralTextures::ShadowSize);
    // Corner slices stop just short of the centre where the blurred mask is flat.
    constexpr float corner = size * 0.5f - 4.0f;
    const float k = blur / ProceduralTextures::ShadowSigma;
    const float pad = static_cast<float>(ProceduralTextures::ShadowPad) * k;
    const core::Rect outer{rect.x - pad, rect.y - pad, rect.width + pad * 2.0f, rect.height + pad * 2.0f};
    const float cornerW = std::min(corner * k, outer.width * 0.5f);
    const float cornerH = std::min(corner * k, outer.height * 0.5f);
    const float dx[4] = {outer.x, outer.x + cornerW, outer.x + outer.width - cornerW, outer.x + outer.width};
    const float dy[4] = {outer.y, outer.y + cornerH, outer.y + outer.height - cornerH, outer.y + outer.height};
    const float sx[4] = {0.0f, cornerW / k, size - cornerW / k, size};
    const float sy[4] = {0.0f, cornerH / k, size - cornerH / k, size};

    // Aliased mode makes adjacent slices share edges exactly, so no seams appear.
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_ALIASED);
    for (int row = 0; row < 3; ++row) {
        for (int col = 0; col < 3; ++col) {
            if (dx[col + 1] - dx[col] <= 0.0f || dy[row + 1] - dy[row] <= 0.0f) {
                continue;
            }
            const D2D1_RECT_F dest = D2D1::RectF(dx[col], dy[row], dx[col + 1], dy[row + 1]);
            const D2D1_RECT_F src = D2D1::RectF(sx[col], sy[row], std::max(sx[col] + 0.5f, sx[col + 1]), std::max(sy[row] + 0.5f, sy[row + 1]));
            target_->FillOpacityMask(mask, brush, D2D1_OPACITY_MASK_CONTENT_GRAPHICS, &dest, &src);
        }
    }
    target_->SetAntialiasMode(D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
}

RenderContext::TextFormatEntry* RenderContext::Format(const TextStyle& style) {
    if (!dwriteFactory_) {
        return nullptr;
    }
    const std::uint64_t key = (static_cast<std::uint64_t>(style.family) << 40) |
        (static_cast<std::uint64_t>(style.weight) << 24) |
        static_cast<std::uint64_t>(style.size * 16.0f);
    auto it = formats_.find(key);
    if (it == formats_.end()) {
        TextFormatEntry entry;
        if (FAILED(dwriteFactory_->CreateTextFormat(
                FamilyName(style.family),
                nullptr,
                style.weight,
                DWRITE_FONT_STYLE_NORMAL,
                DWRITE_FONT_STRETCH_NORMAL,
                style.size,
                L"zh-CN",
                entry.format.ReleaseAndGetAddressOf()))) {
            return nullptr;
        }
        dwriteFactory_->CreateEllipsisTrimmingSign(entry.format.Get(), entry.ellipsis.ReleaseAndGetAddressOf());
        it = formats_.emplace(key, std::move(entry)).first;
    }

    IDWriteTextFormat* format = it->second.format.Get();
    format->SetTextAlignment(style.align);
    format->SetParagraphAlignment(style.valign);
    format->SetWordWrapping(style.wrap ? DWRITE_WORD_WRAPPING_WRAP : DWRITE_WORD_WRAPPING_NO_WRAP);
    if (style.lineHeight > 0.0f) {
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_UNIFORM, style.lineHeight, style.lineHeight * 0.78f);
    } else {
        format->SetLineSpacing(DWRITE_LINE_SPACING_METHOD_DEFAULT, 0.0f, 0.0f);
    }
    DWRITE_TRIMMING trimming{};
    trimming.granularity = style.ellipsis ? DWRITE_TRIMMING_GRANULARITY_CHARACTER : DWRITE_TRIMMING_GRANULARITY_NONE;
    format->SetTrimming(&trimming, style.ellipsis ? it->second.ellipsis.Get() : nullptr);
    return &it->second;
}

void RenderContext::DrawTextUtf8(const std::string& text, const core::Rect& rect, const TextStyle& style, D2D1_COLOR_F color) {
    DrawTextUtf8(text, rect, style, Solid(color));
}

void RenderContext::DrawTextUtf8(const std::string& text, const core::Rect& rect, const TextStyle& style, ID2D1Brush* brush) {
    if (!target_ || !brush || text.empty()) {
        return;
    }
    TextFormatEntry* entry = Format(style);
    if (!entry) {
        return;
    }
    const std::wstring wide = Utf8ToWide(text);
    target_->DrawTextW(wide.c_str(), static_cast<UINT32>(wide.size()), entry->format.Get(), ToRectF(rect), brush);
}

void RenderContext::DrawTextUtf8(
    const std::string& text,
    const core::Rect& rect,
    float fontSize,
    D2D1_COLOR_F color,
    DWRITE_TEXT_ALIGNMENT align,
    DWRITE_PARAGRAPH_ALIGNMENT valign) {
    TextStyle style;
    style.size = fontSize;
    style.align = align;
    style.valign = valign;
    DrawTextUtf8(text, rect, style, color);
}

core::Size RenderContext::MeasureText(const std::string& text, const TextStyle& style, float maxWidth) {
    TextFormatEntry* entry = Format(style);
    if (!entry || text.empty()) {
        return {};
    }
    const std::wstring wide = Utf8ToWide(text);
    ComPtr<IDWriteTextLayout> layout;
    if (FAILED(dwriteFactory_->CreateTextLayout(
            wide.c_str(), static_cast<UINT32>(wide.size()), entry->format.Get(), maxWidth, 4096.0f, layout.ReleaseAndGetAddressOf()))) {
        return {};
    }
    DWRITE_TEXT_METRICS metrics{};
    layout->GetMetrics(&metrics);
    return {metrics.widthIncludingTrailingWhitespace, metrics.height};
}

ComPtr<IDWriteTextLayout> RenderContext::CreateTextLayout(const std::wstring& text, const TextStyle& style, float maxWidth, float maxHeight) {
    ComPtr<IDWriteTextLayout> layout;
    TextFormatEntry* entry = Format(style);
    if (!entry) {
        return layout;
    }
    dwriteFactory_->CreateTextLayout(
        text.c_str(), static_cast<UINT32>(text.size()), entry->format.Get(), maxWidth, maxHeight, layout.ReleaseAndGetAddressOf());
    return layout;
}

void RenderContext::DrawTextLayout(IDWriteTextLayout* layout, core::Point origin, D2D1_COLOR_F color) {
    ID2D1Brush* brush = Solid(color);
    if (!target_ || !layout || !brush) {
        return;
    }
    target_->DrawTextLayout(D2D1::Point2F(origin.x, origin.y), layout, brush);
}

void RenderContext::DrawBitmap(ID2D1Bitmap* bitmap, const core::Rect& dest, const D2D1_RECT_U* source, float opacity) {
    if (!target_ || !bitmap) {
        return;
    }
    if (source) {
        DrawBitmap(bitmap, dest, D2D1::RectF(
            static_cast<float>(source->left),
            static_cast<float>(source->top),
            static_cast<float>(source->right),
            static_cast<float>(source->bottom)), opacity);
        return;
    }
    target_->DrawBitmap(bitmap, ToRectF(dest), opacity * opacity_, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, nullptr);
}

void RenderContext::DrawBitmap(ID2D1Bitmap* bitmap, const core::Rect& dest, const D2D1_RECT_F& source, float opacity) {
    if (!target_ || !bitmap) {
        return;
    }
    target_->DrawBitmap(bitmap, ToRectF(dest), opacity * opacity_, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR, &source);
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
