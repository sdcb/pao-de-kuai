#pragma once

#include "core/Geometry.h"

#include <string>

#include <windows.h>

namespace pdk::app {

// IMM32 bridge for the self-drawn text fields. The IME stays detached from the
// window unless a field has focus, so it never pops up over the card table.
class ImeInput {
public:
    void Attach(HWND hwnd);
    void SetEnabled(bool enabled);
    bool Enabled() const { return enabled_; }
    // Caret in logical coordinates; moves the composition and candidate windows.
    void SetCaret(const core::Rect& caret, const core::ViewTransform& view);

    bool ReadComposition(std::wstring& text, int& cursor) const;
    bool ReadResult(std::wstring& text) const;

private:
    HWND hwnd_{};
    bool enabled_{true};
    RECT lastCaret_{-1, -1, -1, -1};
};

} // namespace pdk::app
