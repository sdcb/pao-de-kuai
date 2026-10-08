#pragma once

/*
 * The four form widgets the settings overlay is built from: TextField (single-line editor
 * with caret, selection, clipboard and inline IME composition), Slider, Segmented and Toggle.
 *
 * Pure C.  What changed and why:
 *   - `std::wstring text_` / `composition_` became WStr (core/Str.h).  The field owns both
 *     buffers, so the C side gets a TextField_Destroy; the C++ struct used to free them in its
 *     destructor.
 *   - `graphics::ComPtr<IDWriteTextLayout> layout_` became a raw `PDK_IDWriteTextLayout *`
 *     owned by the struct and dropped with PDK_RELEASE, the same shape ProceduralTextures and
 *     SpriteAtlas use.
 *   - `std::vector<std::string> options` on Segmented became a fixed array of char buffers
 *     plus a count.  The tree only ever has two options ("基础"/"强力"), and the old code
 *     copied them out of a local vector, so the struct has to own the text rather than borrow
 *     a pointer.  Segmented_SetOption is the only writer, and it copies with Str_CopyTo.
 *   - The structs need the usual `#ifdef __cplusplus` default constructors: their members had
 *     brace initialisers (`maxChars{12}`, `value{0.0f}`, `selected{0}`, `on{false}`,
 *     `layoutDirty_{true}`, ...), and a bare C struct would leave `TextField field;`
 *     indeterminate.  Each *_Init function is the single source of truth for those defaults
 *     and the constructor calls it, so the two cannot drift.
 *   - `Rect` is passed as `const Rect *` and ints/floats/bools stay as they are.
 *   - The drawing and input entry points lost their C++ `this`; the widget is the first
 *     argument, like Widgets.h and CardView.h.  They take the C `RenderContext`, so this
 *     header includes graphics/D2DContext.h and NOT graphics/d2d_c.h -- see the comment in
 *     graphics/ProceduralTextures.h for why the generated shim must stay out of any header a
 *     C++ translation unit includes.
 *
 * The C++ callers keep `ui::TextField`, `ui::Slider`, ... and the old method spellings through
 * ui/CppCompat.h.
 */

#include "core/Geometry.h"
#include "core/KeyEvent.h"
#include "core/Str.h"
#include "graphics/D2DContext.h"
#include "ui/Anim.h"
#include "ui/Theme.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

enum { PDK_TEXT_FIELD_PLACEHOLDER_CAP = 64 };
enum { PDK_SEGMENTED_MAX = 4 };
enum { PDK_SEGMENTED_OPTION_CAP = 32 };

typedef struct TextField TextField;
typedef struct Slider Slider;
typedef struct Segmented Segmented;
typedef struct Toggle Toggle;

static inline void TextField_Init(TextField *field);
static inline void Slider_Init(Slider *slider);
static inline void Segmented_Init(Segmented *segmented);
static inline void Toggle_Init(Toggle *toggle);

struct TextField {
    Rect rect;
    char placeholder[PDK_TEXT_FIELD_PLACEHOLDER_CAP];
    int maxChars;
    HWND clipboardOwner;

    /* ---- internal ---- */
    WStr text;
    WStr composition;
    int compositionCursor;
    int caret;
    int anchor;
    bool focused;
    bool hover;
    bool dragging;
    float focusT;
    float hoverT;
    float blink;
    bool layoutDirty;
    PDK_IDWriteTextLayout *layout; /* owned; PDK_RELEASE it, or call TextField_Destroy */
    float lineHeight;
    Rect caretRect;

#ifdef __cplusplus
    /* ---- TEMPORARY C++ shim: delete with src/ui/CppCompat.h ---- */
    TextField() { TextField_Init(this); }
#endif
};

struct Slider {
    Rect rect;
    float value;
    bool hover;
    bool dragging;
    float hoverT;

#ifdef __cplusplus
    /* ---- TEMPORARY C++ shim: delete with src/ui/CppCompat.h ---- */
    Slider() { Slider_Init(this); }
#endif
};

struct Segmented {
    Rect rect;
    char options[PDK_SEGMENTED_MAX][PDK_SEGMENTED_OPTION_CAP];
    int optionCount;
    int selected;
    int hover;
    float slide;

#ifdef __cplusplus
    /* ---- TEMPORARY C++ shim: delete with src/ui/CppCompat.h ---- */
    Segmented() { Segmented_Init(this); }
#endif
};

struct Toggle {
    Rect rect;
    bool on;
    bool hover;
    float t;
    float hoverT;

#ifdef __cplusplus
    /* ---- TEMPORARY C++ shim: delete with src/ui/CppCompat.h ---- */
    Toggle() { Toggle_Init(this); }
#endif
};

static inline void TextField_Init(TextField *field)
{
    field->rect.x = 0.0f;
    field->rect.y = 0.0f;
    field->rect.width = 0.0f;
    field->rect.height = 0.0f;
    field->placeholder[0] = '\0';
    field->maxChars = 12;
    field->clipboardOwner = NULL;
    WStr_Init(&field->text);
    WStr_Init(&field->composition);
    field->compositionCursor = 0;
    field->caret = 0;
    field->anchor = 0;
    field->focused = false;
    field->hover = false;
    field->dragging = false;
    field->focusT = 0.0f;
    field->hoverT = 0.0f;
    field->blink = 0.0f;
    field->layoutDirty = true;
    field->layout = NULL;
    field->lineHeight = 24.0f;
    field->caretRect.x = 0.0f;
    field->caretRect.y = 0.0f;
    field->caretRect.width = 0.0f;
    field->caretRect.height = 0.0f;
}

static inline void Slider_Init(Slider *slider)
{
    slider->rect.x = 0.0f;
    slider->rect.y = 0.0f;
    slider->rect.width = 0.0f;
    slider->rect.height = 0.0f;
    slider->value = 0.0f;
    slider->hover = false;
    slider->dragging = false;
    slider->hoverT = 0.0f;
}

static inline void Segmented_Init(Segmented *segmented)
{
    int i;
    int j;

    segmented->rect.x = 0.0f;
    segmented->rect.y = 0.0f;
    segmented->rect.width = 0.0f;
    segmented->rect.height = 0.0f;
    for (i = 0; i < PDK_SEGMENTED_MAX; ++i) {
        for (j = 0; j < PDK_SEGMENTED_OPTION_CAP; ++j) {
            segmented->options[i][j] = '\0';
        }
    }
    segmented->optionCount = 0;
    segmented->selected = 0;
    segmented->hover = -1;
    segmented->slide = 0.0f;
}

static inline void Toggle_Init(Toggle *toggle)
{
    toggle->rect.x = 0.0f;
    toggle->rect.y = 0.0f;
    toggle->rect.width = 0.0f;
    toggle->rect.height = 0.0f;
    toggle->on = false;
    toggle->hover = false;
    toggle->t = 0.0f;
    toggle->hoverT = 0.0f;
}

/* Releases the field's layout and string buffers.  Safe to call more than once; the field
 * must be re-Init'ed before it is used again. */
void TextField_Destroy(TextField *field);

void TextField_SetUtf8(TextField *field, const char *text);
/* Appends the UTF-8 form of the field text to `out` (caller owned). */
void TextField_Utf8(const TextField *field, Str *out);
/* The same text into a fixed buffer, truncating and always NUL terminating.  Returns the byte
 * count written (excluding the terminator). */
int TextField_Utf8To(const TextField *field, char *out, int cap);
static inline bool TextField_Focused(const TextField *field)
{
    return field->focused;
}
void TextField_SetFocused(TextField *field, bool focused);

void TextField_Update(TextField *field, float dt);
/* Not const: drawing refreshes the cached layout and caret rectangle, which are internal
 * state, exactly like the old member function that was const only by accident. */
void TextField_Draw(TextField *field, RenderContext *context);
void TextField_UpdateHover(TextField *field, float x, float y);
/* Focuses the field and places the caret; returns false when the click is outside. */
bool TextField_OnMouseDown(TextField *field, float x, float y, bool shift);
void TextField_OnMouseMove(TextField *field, float x, float y);
void TextField_OnMouseUp(TextField *field);
bool TextField_OnKeyDown(TextField *field, const KeyEvent *key);
void TextField_Insert(TextField *field, const WStr *text);
void TextField_SetComposition(TextField *field, const WStr *text, int cursor);
/* Caret rectangle in logical coordinates; false when there is nothing to show. */
bool TextField_CaretRect(const TextField *field, Rect *out);

void Slider_Update(Slider *slider, float dt);
void Slider_Draw(const Slider *slider, RenderContext *context);
bool Slider_OnMouseDown(Slider *slider, float x, float y);
/* Returns true when the value changed during a drag. */
bool Slider_OnMouseMove(Slider *slider, float x, float y);
/* Returns true when a drag ended. */
bool Slider_OnMouseUp(Slider *slider);

/* Replaces the option list.  `options` is an array of `count` UTF-8 strings; the text is
 * copied, so the caller keeps ownership of its own buffers. */
void Segmented_SetOptions(Segmented *segmented, const char *const *options, int count);
void Segmented_Update(Segmented *segmented, float dt);
void Segmented_Draw(const Segmented *segmented, RenderContext *context);
void Segmented_UpdateHover(Segmented *segmented, float x, float y);
/* Returns true when the selection changed. */
bool Segmented_OnMouseDown(Segmented *segmented, float x, float y);

void Toggle_Update(Toggle *toggle, float dt);
void Toggle_Draw(const Toggle *toggle, RenderContext *context);
void Toggle_UpdateHover(Toggle *toggle, float x, float y);
bool Toggle_OnMouseDown(Toggle *toggle, float x, float y);

#ifdef __cplusplus
}
#endif
