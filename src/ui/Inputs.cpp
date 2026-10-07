#include "ui/Inputs.h"

#include "ui/Anim.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace pdk::ui {
namespace {

constexpr D2D1_COLOR_F Black = {0.0f, 0.0f, 0.0f, 1.0f};
constexpr D2D1_COLOR_F White = {1.0f, 1.0f, 1.0f, 1.0f};
constexpr float FieldPadX = 16.0f;
constexpr float CaretBlinkPeriod = 1.06f;

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(std::max(size, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(std::max(size, 0)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

void CopyToClipboard(HWND owner, const std::wstring& text) {
    if (text.empty() || !OpenClipboard(owner)) {
        return;
    }
    EmptyClipboard();
    const std::size_t bytes = (text.size() + 1) * sizeof(wchar_t);
    if (HGLOBAL memory = GlobalAlloc(GMEM_MOVEABLE, bytes)) {
        if (void* data = GlobalLock(memory)) {
            std::memcpy(data, text.c_str(), bytes);
            GlobalUnlock(memory);
            if (!SetClipboardData(CF_UNICODETEXT, memory)) {
                GlobalFree(memory);
            }
        } else {
            GlobalFree(memory);
        }
    }
    CloseClipboard();
}

std::wstring ReadClipboard(HWND owner) {
    std::wstring text;
    if (!OpenClipboard(owner)) {
        return text;
    }
    if (HANDLE handle = GetClipboardData(CF_UNICODETEXT)) {
        if (const auto* data = static_cast<const wchar_t*>(GlobalLock(handle))) {
            text = data;
            GlobalUnlock(handle);
        }
    }
    CloseClipboard();
    return text;
}

graphics::TextStyle FieldStyle() {
    graphics::TextStyle style = Text(19.0f);
    style.wrap = false;
    return style;
}

Rect Inset(const Rect& rect, float d) {
    return {rect.x + d, rect.y + d, rect.width - d * 2.0f, rect.height - d * 2.0f};
}

void DrawKnob(graphics::RenderContext& context, Point c, float r, float glow) {
    if (glow > 0.01f) {
        context.DrawShadow({c.x - r, c.y - r, r * 2.0f, r * 2.0f}, 7.0f, WithAlpha(theme::Gold, 0.45f * glow));
    }
    context.DrawShadow({c.x - r + 2.0f, c.y - r + 4.0f, r * 2.0f - 4.0f, r * 2.0f - 4.0f}, 3.5f, WithAlpha(Black, 0.55f));
    const Rect knob{c.x - r, c.y - r, r * 2.0f, r * 2.0f};
    context.FillEllipse(knob, context.Linear({c.x, c.y - r}, {c.x, c.y + r},
        {{0.0f, LerpColor(theme::Ivory, White, 0.3f * glow)}, {1.0f, theme::GoldLight}}));
    context.StrokeEllipse(Inset(knob, 0.5f), WithAlpha(theme::GoldDeep, 0.9f), 1.0f);
}

} // namespace

void TextField::SetUtf8(const std::string& text) {
    text_ = Utf8ToWide(text);
    caret_ = anchor_ = static_cast<int>(text_.size());
    composition_.clear();
    layoutDirty_ = true;
}

std::string TextField::Utf8() const {
    return WideToUtf8(text_);
}

void TextField::SetFocused(bool focused) {
    if (focused_ == focused) {
        return;
    }
    focused_ = focused;
    blink_ = 0.0f;
    if (!focused) {
        composition_.clear();
        anchor_ = caret_;
        dragging_ = false;
        layoutDirty_ = true;
    }
}

void TextField::Update(float dt) {
    focusT_ = Approach(focusT_, focused_ ? 1.0f : 0.0f, 14.0f, dt);
    hoverT_ = Approach(hoverT_, hover_ ? 1.0f : 0.0f, 16.0f, dt);
    blink_ += dt;
}

Point TextField::TextOrigin() const {
    return {rect.x + FieldPadX, rect.y + (rect.height - lineHeight_) * 0.5f};
}

void TextField::Draw(graphics::RenderContext& context) {
    const float radius = 12.0f;
    if (focusT_ > 0.01f) {
        context.DrawShadow(rect, 9.0f, WithAlpha(theme::Gold, 0.28f * focusT_));
    }
    context.FillRoundedRect(rect, radius, context.Linear({rect.x, rect.y}, {rect.x, rect.y + rect.height},
        {{0.0f, WithAlpha(theme::RoomDeep, 0.92f)}, {1.0f, WithAlpha(LerpColor(theme::Ink, theme::InkRaised, hoverT_), 0.92f)}}));
    context.StrokeRoundedRect(Inset(rect, 0.5f), radius,
        WithAlpha(LerpColor(theme::Gold, theme::GoldLight, focusT_), 0.26f + 0.16f * hoverT_ + 0.5f * focusT_), 1.0f + 0.4f * focusT_);

    std::wstring display = text_;
    const bool composing = !composition_.empty();
    if (composing) {
        display.insert(static_cast<std::size_t>(caret_), composition_);
    }
    if (layoutDirty_ || !layout_) {
        layout_ = context.CreateTextLayout(display, FieldStyle(), 4096.0f, rect.height);
        layoutDirty_ = false;
        if (layout_) {
            if (composing) {
                layout_->SetUnderline(TRUE, DWRITE_TEXT_RANGE{static_cast<UINT32>(caret_), static_cast<UINT32>(composition_.size())});
            }
            DWRITE_TEXT_METRICS metrics{};
            layout_->GetMetrics(&metrics);
            if (metrics.height > 1.0f) {
                lineHeight_ = metrics.height;
            }
        }
    }

    const Point origin = TextOrigin();
    context.PushClip(Inset(rect, 4.0f));
    if (display.empty() && !placeholder.empty()) {
        graphics::TextStyle style = FieldStyle();
        style.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        context.DrawTextUtf8(placeholder, {origin.x, rect.y, rect.width - FieldPadX * 2.0f, rect.height}, style, theme::Faint);
    }
    if (layout_) {
        if (HasSelection() && !composing) {
            const int from = std::min(caret_, anchor_);
            const int length = std::abs(caret_ - anchor_);
            DWRITE_HIT_TEST_METRICS ranges[8]{};
            UINT32 count = 0;
            if (SUCCEEDED(layout_->HitTestTextRange(static_cast<UINT32>(from), static_cast<UINT32>(length), 0.0f, 0.0f, ranges, 8, &count))) {
                for (UINT32 i = 0; i < count; ++i) {
                    context.FillRoundedRect({origin.x + ranges[i].left, origin.y + ranges[i].top, ranges[i].width, ranges[i].height},
                        3.0f, WithAlpha(theme::Gold, 0.30f));
                }
            }
        }
        context.DrawTextLayout(layout_.Get(), origin, theme::Ivory);

        const int displayCaret = caret_ + (composing ? compositionCursor_ : 0);
        FLOAT px = 0.0f;
        FLOAT py = 0.0f;
        DWRITE_HIT_TEST_METRICS metrics{};
        layout_->HitTestTextPosition(static_cast<UINT32>(displayCaret), FALSE, &px, &py, &metrics);
        const float height = metrics.height > 1.0f ? metrics.height : lineHeight_;
        caretRect_ = {origin.x + px, origin.y + py, 1.6f, height};
        if (focused_ && std::fmod(blink_, CaretBlinkPeriod) < CaretBlinkPeriod * 0.58f) {
            context.FillRect({caretRect_.x - 0.3f, caretRect_.y + 2.0f, caretRect_.width, caretRect_.height - 4.0f}, theme::GoldLight);
        }
    }
    context.PopClip();
}

int TextField::HitIndex(float x, float y) const {
    if (!layout_) {
        return static_cast<int>(text_.size());
    }
    const Point origin = TextOrigin();
    BOOL trailing = FALSE;
    BOOL inside = FALSE;
    DWRITE_HIT_TEST_METRICS metrics{};
    layout_->HitTestPoint(x - origin.x, y - origin.y, &trailing, &inside, &metrics);
    const int index = static_cast<int>(metrics.textPosition) + (trailing ? static_cast<int>(metrics.length) : 0);
    return ClampToBoundary(std::clamp(index, 0, static_cast<int>(text_.size())));
}

int TextField::ClampToBoundary(int index) const {
    if (index > 0 && index < static_cast<int>(text_.size()) && IS_LOW_SURROGATE(text_[static_cast<std::size_t>(index)])) {
        return index - 1;
    }
    return index;
}

int TextField::PrevBoundary(int index) const {
    if (index <= 0) {
        return 0;
    }
    --index;
    if (index > 0 && IS_LOW_SURROGATE(text_[static_cast<std::size_t>(index)]) && IS_HIGH_SURROGATE(text_[static_cast<std::size_t>(index - 1)])) {
        --index;
    }
    return index;
}

int TextField::NextBoundary(int index) const {
    const int size = static_cast<int>(text_.size());
    if (index >= size) {
        return size;
    }
    ++index;
    if (index < size && IS_LOW_SURROGATE(text_[static_cast<std::size_t>(index)])) {
        ++index;
    }
    return index;
}

int TextField::CharCount(const std::wstring& text) const {
    return static_cast<int>(std::count_if(text.begin(), text.end(), [](wchar_t c) { return !IS_LOW_SURROGATE(c); }));
}

void TextField::MoveCaret(int index, bool extend) {
    caret_ = std::clamp(index, 0, static_cast<int>(text_.size()));
    if (!extend) {
        anchor_ = caret_;
    }
    blink_ = 0.0f;
}

void TextField::DeleteSelection() {
    if (!HasSelection()) {
        return;
    }
    const int from = std::min(caret_, anchor_);
    text_.erase(static_cast<std::size_t>(from), static_cast<std::size_t>(std::abs(caret_ - anchor_)));
    caret_ = anchor_ = from;
    layoutDirty_ = true;
}

bool TextField::OnMouseDown(float x, float y, bool shift) {
    if (!Rect_Contains(&rect, x, y)) {
        SetFocused(false);
        return false;
    }
    SetFocused(true);
    if (composition_.empty()) {
        MoveCaret(HitIndex(x, y), shift);
        dragging_ = true;
    }
    return true;
}

void TextField::OnMouseMove(float x, float y) {
    if (dragging_ && composition_.empty()) {
        MoveCaret(HitIndex(x, y), true);
    }
}

bool TextField::OnKeyDown(const core::KeyEvent& key) {
    if (!focused_ || !composition_.empty()) {
        return false;
    }
    const int size = static_cast<int>(text_.size());
    switch (key.key) {
    case VK_LEFT:
        MoveCaret(HasSelection() && !key.shift ? std::min(caret_, anchor_) : PrevBoundary(caret_), key.shift);
        return true;
    case VK_RIGHT:
        MoveCaret(HasSelection() && !key.shift ? std::max(caret_, anchor_) : NextBoundary(caret_), key.shift);
        return true;
    case VK_HOME:
        MoveCaret(0, key.shift);
        return true;
    case VK_END:
        MoveCaret(size, key.shift);
        return true;
    case VK_BACK:
        if (!HasSelection() && caret_ > 0) {
            anchor_ = PrevBoundary(caret_);
        }
        DeleteSelection();
        blink_ = 0.0f;
        return true;
    case VK_DELETE:
        if (!HasSelection() && caret_ < size) {
            anchor_ = NextBoundary(caret_);
        }
        DeleteSelection();
        blink_ = 0.0f;
        return true;
    default:
        break;
    }
    if (!key.ctrl) {
        return false;
    }
    const int from = std::min(caret_, anchor_);
    const std::wstring selected = text_.substr(static_cast<std::size_t>(from), static_cast<std::size_t>(std::abs(caret_ - anchor_)));
    switch (key.key) {
    case 'A':
        anchor_ = 0;
        caret_ = size;
        return true;
    case 'C':
        CopyToClipboard(clipboardOwner, selected);
        return true;
    case 'X':
        CopyToClipboard(clipboardOwner, selected);
        DeleteSelection();
        return true;
    case 'V':
        Insert(ReadClipboard(clipboardOwner));
        return true;
    default:
        return false;
    }
}

void TextField::Insert(const std::wstring& text) {
    if (!focused_) {
        return;
    }
    DeleteSelection();
    int capacity = maxChars - CharCount(text_);
    std::wstring accepted;
    for (std::size_t i = 0; i < text.size() && capacity > 0; ++i) {
        const wchar_t c = text[i];
        if (c < 0x20 || c == 0x7F) {
            continue;
        }
        if (IS_HIGH_SURROGATE(c)) {
            if (i + 1 < text.size() && IS_LOW_SURROGATE(text[i + 1])) {
                accepted.push_back(c);
                accepted.push_back(text[++i]);
                --capacity;
            }
            continue;
        }
        if (IS_LOW_SURROGATE(c)) {
            continue;
        }
        accepted.push_back(c);
        --capacity;
    }
    text_.insert(static_cast<std::size_t>(caret_), accepted);
    MoveCaret(caret_ + static_cast<int>(accepted.size()), false);
    layoutDirty_ = true;
}

void TextField::SetComposition(const std::wstring& text, int cursor) {
    if (!text.empty() && composition_.empty()) {
        DeleteSelection();
    }
    composition_ = text;
    compositionCursor_ = std::clamp(cursor, 0, static_cast<int>(text.size()));
    blink_ = 0.0f;
    layoutDirty_ = true;
}

void Slider::Update(float dt) {
    hoverT = Approach(hoverT, hover || dragging ? 1.0f : 0.0f, 16.0f, dt);
}

void Slider::Draw(graphics::RenderContext& context) const {
    const float cy = rect.y + rect.height * 0.5f;
    const Rect track{rect.x, cy - 3.0f, rect.width, 6.0f};
    context.FillRoundedRect(track, 3.0f, WithAlpha(theme::RoomDeep, 0.9f));
    context.StrokeRoundedRect(track, 3.0f, WithAlpha(theme::Gold, 0.22f), 1.0f);
    const float kx = rect.x + rect.width * Clamp01(value);
    if (kx > rect.x + 1.0f) {
        const Rect fill{rect.x, track.y, kx - rect.x, track.height};
        context.FillRoundedRect(fill, 3.0f, context.Linear({fill.x, fill.y}, {fill.x + std::max(fill.width, 1.0f), fill.y},
            {{0.0f, theme::GoldDeep}, {1.0f, theme::GoldLight}}));
    }
    for (int i = 1; i < 4; ++i) {
        const float tx = rect.x + rect.width * static_cast<float>(i) * 0.25f;
        context.DrawLine({tx, cy + 9.0f}, {tx, cy + 13.0f}, WithAlpha(theme::Gold, 0.28f), 1.0f);
    }
    DrawKnob(context, {kx, cy}, 11.0f + 1.5f * hoverT, hoverT);
}

bool Slider::SetFromX(float x) {
    const float next = Clamp01((x - rect.x) / std::max(rect.width, 1.0f));
    const float stepped = static_cast<float>(RoundToInt(next * 100.0f)) / 100.0f;
    if (std::fabs(stepped - value) < 0.0001f) {
        return false;
    }
    value = stepped;
    return true;
}

bool Slider::OnMouseDown(float x, float y) {
    const Rect hit{rect.x - 14.0f, rect.y - 6.0f, rect.width + 28.0f, rect.height + 12.0f};
    if (!Rect_Contains(&hit, x, y)) {
        return false;
    }
    dragging = true;
    SetFromX(x);
    return true;
}

bool Slider::OnMouseMove(float x, float y) {
    const Rect hit{rect.x - 14.0f, rect.y - 6.0f, rect.width + 28.0f, rect.height + 12.0f};
    hover = Rect_Contains(&hit, x, y);
    return dragging && SetFromX(x);
}

bool Slider::OnMouseUp() {
    const bool was = dragging;
    dragging = false;
    return was;
}

void Segmented::Update(float dt) {
    slide = Approach(slide, static_cast<float>(selected), 18.0f, dt);
}

int Segmented::HitTest(float x, float y) const {
    if (options.empty() || !Rect_Contains(&rect, x, y)) {
        return -1;
    }
    const float width = rect.width / static_cast<float>(options.size());
    return std::clamp(static_cast<int>((x - rect.x) / width), 0, static_cast<int>(options.size()) - 1);
}

void Segmented::UpdateHover(float x, float y) {
    hover = HitTest(x, y);
}

bool Segmented::OnMouseDown(float x, float y) {
    const int hit = HitTest(x, y);
    if (hit < 0 || hit == selected) {
        return false;
    }
    selected = hit;
    return true;
}

void Segmented::Draw(graphics::RenderContext& context) const {
    if (options.empty()) {
        return;
    }
    const float radius = rect.height * 0.5f;
    context.FillRoundedRect(rect, radius, WithAlpha(theme::RoomDeep, 0.9f));
    context.StrokeRoundedRect(Inset(rect, 0.5f), radius, WithAlpha(theme::Gold, 0.28f), 1.0f);

    const float width = rect.width / static_cast<float>(options.size());
    const Rect pill{rect.x + 3.0f + width * slide, rect.y + 3.0f, width - 6.0f, rect.height - 6.0f};
    context.DrawShadow({pill.x + 2.0f, pill.y + 3.0f, pill.width - 4.0f, pill.height - 2.0f}, 4.0f, WithAlpha(Black, 0.45f));
    context.FillRoundedRect(pill, pill.height * 0.5f, context.Linear({pill.x, pill.y}, {pill.x, pill.y + pill.height},
        {{0.0f, theme::GoldLight}, {0.6f, theme::Gold}, {1.0f, theme::GoldDeep}}));

    graphics::TextStyle style = Centered(Text(16.0f, DWRITE_FONT_WEIGHT_SEMI_BOLD));
    style.wrap = false;
    for (std::size_t i = 0; i < options.size(); ++i) {
        const float weight = Clamp01(1.0f - std::fabs(slide - static_cast<float>(i)));
        const D2D1_COLOR_F idle = static_cast<int>(i) == hover ? theme::Ivory : theme::Muted;
        const Rect cell{rect.x + width * static_cast<float>(i), rect.y, width, rect.height};
        context.DrawTextUtf8(options[i], cell, style, LerpColor(idle, theme::GoldInk, weight));
    }
}

void Toggle::Update(float dt) {
    t = Approach(t, on ? 1.0f : 0.0f, 16.0f, dt);
    hoverT = Approach(hoverT, hover ? 1.0f : 0.0f, 16.0f, dt);
}

void Toggle::Draw(graphics::RenderContext& context) const {
    const float radius = rect.height * 0.5f;
    context.FillRoundedRect(rect, radius, WithAlpha(theme::RoomDeep, 0.9f));
    if (t > 0.01f) {
        context.PushOpacity(t);
        context.FillRoundedRect(rect, radius, context.Linear({rect.x, rect.y}, {rect.x, rect.y + rect.height},
            {{0.0f, theme::Gold}, {1.0f, theme::GoldDeep}}));
        context.PopOpacity();
    }
    context.StrokeRoundedRect(Inset(rect, 0.5f), radius, WithAlpha(theme::Gold, 0.3f + 0.3f * hoverT), 1.0f);
    const float r = radius - 4.0f;
    const float cx = Lerp(rect.x + radius, rect.x + rect.width - radius, EaseInOutSine(t));
    DrawKnob(context, {cx, rect.y + radius}, r, hoverT * 0.6f);
}

bool Toggle::OnMouseDown(float x, float y) {
    if (!Rect_Contains(&rect, x, y)) {
        return false;
    }
    on = !on;
    return true;
}

} // namespace pdk::ui
