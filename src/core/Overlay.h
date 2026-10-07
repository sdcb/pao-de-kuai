#pragma once

#include "core/Geometry.h"
#include "core/KeyEvent.h"

#include <string>

namespace pdk::graphics {
class RenderContext;
}

namespace pdk::core {

/* The struct itself moved to core/KeyEvent.h so the pure-C src/ui/Inputs.h can name it; the
 * `pdk::core` spelling the C++ callers use stays valid through this alias. */
using ::KeyEvent;

class Overlay {
public:
    virtual ~Overlay() = default;

    virtual void Update(float dt) = 0;
    virtual void Render(graphics::RenderContext& context) = 0;

    virtual bool BlocksInputBelow() const = 0;
    virtual bool OnMouseMove(float x, float y) { (void)x; (void)y; return false; }
    virtual bool OnMouseDown(float x, float y) { (void)x; (void)y; return false; }
    virtual bool OnMouseUp(float x, float y) { (void)x; (void)y; return false; }

    virtual bool OnKeyDown(const KeyEvent& key) { (void)key; return false; }
    // Committed text: typed characters and IME results.
    virtual bool OnText(const std::wstring& text) { (void)text; return false; }

    // True while a text field has focus; the app only enables the IME then.
    virtual bool WantsTextInput() const { return false; }
    // In-progress IME composition; empty text ends it.
    virtual void OnImeComposition(const std::wstring& text, int cursor) { (void)text; (void)cursor; }
    // Caret rectangle in logical coordinates, used to place the IME candidate window.
    virtual bool TextCaretRect(Rect& caret) const { (void)caret; return false; }
};

} // namespace pdk::core
