#pragma once

#include "core/Geometry.h"
#include "core/Overlay.h"
#include "graphics/ComPtr.h"
#include "graphics/D2DContext.h"

#include <string>
#include <vector>

#include <windows.h>

namespace pdk::ui {

// Single-line editor with caret, selection, clipboard and inline IME composition.
class TextField {
public:
    Rect rect;
    std::string placeholder;
    int maxChars{12};
    HWND clipboardOwner{};

    void SetUtf8(const std::string& text);
    std::string Utf8() const;
    bool Focused() const { return focused_; }
    void SetFocused(bool focused);

    void Update(float dt);
    void Draw(graphics::RenderContext& context);
    void UpdateHover(float x, float y) { hover_ = Rect_Contains(&rect, x, y); }
    // Focuses the field and places the caret; returns false when the click is outside.
    bool OnMouseDown(float x, float y, bool shift);
    void OnMouseMove(float x, float y);
    void OnMouseUp() { dragging_ = false; }
    bool OnKeyDown(const core::KeyEvent& key);
    void Insert(const std::wstring& text);
    void SetComposition(const std::wstring& text, int cursor);
    Rect CaretRect() const { return caretRect_; }

private:
    int HitIndex(float x, float y) const;
    int ClampToBoundary(int index) const;
    int PrevBoundary(int index) const;
    int NextBoundary(int index) const;
    void DeleteSelection();
    bool HasSelection() const { return caret_ != anchor_; }
    void MoveCaret(int index, bool extend);
    int CharCount(const std::wstring& text) const;
    Point TextOrigin() const;

    std::wstring text_;
    std::wstring composition_;
    int compositionCursor_{0};
    int caret_{0};
    int anchor_{0};
    bool focused_{false};
    bool hover_{false};
    bool dragging_{false};
    float focusT_{0.0f};
    float hoverT_{0.0f};
    float blink_{0.0f};
    bool layoutDirty_{true};
    graphics::ComPtr<IDWriteTextLayout> layout_;
    float lineHeight_{24.0f};
    Rect caretRect_{};
};

struct Slider {
    Rect rect;
    float value{0.0f};
    bool hover{false};
    bool dragging{false};
    float hoverT{0.0f};

    void Update(float dt);
    void Draw(graphics::RenderContext& context) const;
    bool OnMouseDown(float x, float y);
    // Returns true when the value changed during a drag.
    bool OnMouseMove(float x, float y);
    // Returns true when a drag ended.
    bool OnMouseUp();

private:
    bool SetFromX(float x);
};

struct Segmented {
    Rect rect;
    std::vector<std::string> options;
    int selected{0};
    int hover{-1};
    float slide{0.0f};

    void Update(float dt);
    void Draw(graphics::RenderContext& context) const;
    void UpdateHover(float x, float y);
    // Returns true when the selection changed.
    bool OnMouseDown(float x, float y);

private:
    int HitTest(float x, float y) const;
};

struct Toggle {
    Rect rect;
    bool on{false};
    bool hover{false};
    float t{0.0f};
    float hoverT{0.0f};

    void Update(float dt);
    void Draw(graphics::RenderContext& context) const;
    void UpdateHover(float x, float y) { hover = Rect_Contains(&rect, x, y); }
    bool OnMouseDown(float x, float y);
};

} // namespace pdk::ui
