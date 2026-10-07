#pragma once

/*
 * IMM32 bridge for the self-drawn text fields.  The IME stays detached from the window
 * unless a field has focus, so it never pops up over the card table.
 *
 * Pure C.  The old class held an HWND, an enabled flag and the last caret rect, i.e. no
 * ownership at all, so it becomes a plain value type with an Init.  The two readers filled a
 * std::wstring by reference and now fill a WStr, which is the project's growable C string.
 */

#include "core/Geometry.h"
#include "core/Str.h"

#include <stdbool.h>

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct ImeInput {
    HWND hwnd;
    bool enabled;
    RECT lastCaret;
} ImeInput;

/* hwnd = NULL, enabled = true, caret = none, matching the old member initialisers. */
void ImeInput_Init(ImeInput *ime);

/* Attaches to a window and leaves the IME detached until a field asks for it. */
void ImeInput_Attach(ImeInput *ime, HWND hwnd);
void ImeInput_SetEnabled(ImeInput *ime, bool enabled);
bool ImeInput_Enabled(const ImeInput *ime);

/* Caret in logical coordinates; moves the composition and candidate windows. */
void ImeInput_SetCaret(ImeInput *ime, const Rect *caret, const ViewTransform *view);

/* Both clear `text` first.  ReadComposition always reports success once a context exists,
 * with an empty string when nothing is being composed. */
bool ImeInput_ReadComposition(const ImeInput *ime, WStr *text, int *cursor);
bool ImeInput_ReadResult(const ImeInput *ime, WStr *text);

#ifdef __cplusplus
}
#endif
