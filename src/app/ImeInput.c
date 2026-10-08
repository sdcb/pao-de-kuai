#include "app/ImeInput.h"

#include "ui/Anim.h"

#include <imm.h>

/* Reads one composition string into `text`, which is cleared first.  Returns false only when
 * there is no input context at all; an absent string is success with an empty result. */
static bool ReadString(HWND hwnd, DWORD index, WStr *text)
{
    HIMC himc;
    LONG bytes;

    WStr_Clear(text);
    himc = ImmGetContext(hwnd);
    if (himc == NULL) {
        return false;
    }
    bytes = ImmGetCompositionStringW(himc, index, NULL, 0);
    if (bytes > 0) {
        const int units = (int)(bytes / (LONG)sizeof(wchar_t));

        if (WStr_Reserve(text, units) && text->data != NULL) {
            ImmGetCompositionStringW(himc, index, text->data, (DWORD)bytes);
            text->data[units] = L'\0';
            text->len = units;
        }
    }
    ImmReleaseContext(hwnd, himc);
    return bytes >= 0;
}

void ImeInput_Init(ImeInput *ime)
{
    ime->hwnd = NULL;
    ime->enabled = true;
    ime->lastCaret.left = -1;
    ime->lastCaret.top = -1;
    ime->lastCaret.right = -1;
    ime->lastCaret.bottom = -1;
}

void ImeInput_Attach(ImeInput *ime, HWND hwnd)
{
    ime->hwnd = hwnd;
    ime->enabled = true;
    ImeInput_SetEnabled(ime, false);
}

void ImeInput_SetEnabled(ImeInput *ime, bool enabled)
{
    if (ime->hwnd == NULL || enabled == ime->enabled) {
        return;
    }
    ime->enabled = enabled;
    ImmAssociateContextEx(ime->hwnd, NULL, enabled ? IACE_DEFAULT : 0);
    ime->lastCaret.left = -1;
    ime->lastCaret.top = -1;
    ime->lastCaret.right = -1;
    ime->lastCaret.bottom = -1;
}

bool ImeInput_Enabled(const ImeInput *ime)
{
    return ime->enabled;
}

void ImeInput_SetCaret(ImeInput *ime, const Rect *caret, const ViewTransform *view)
{
    RECT pixel;
    HIMC himc;
    COMPOSITIONFORM composition;
    CANDIDATEFORM candidate;

    if (ime->hwnd == NULL || !ime->enabled) {
        return;
    }
    pixel.left = RoundToInt(caret->x * view->scale + view->offsetX);
    pixel.top = RoundToInt(caret->y * view->scale + view->offsetY);
    pixel.right = RoundToInt((caret->x + caret->width) * view->scale + view->offsetX);
    pixel.bottom = RoundToInt((caret->y + caret->height) * view->scale + view->offsetY);
    if (pixel.left == ime->lastCaret.left && pixel.top == ime->lastCaret.top &&
        pixel.bottom == ime->lastCaret.bottom) {
        return;
    }
    himc = ImmGetContext(ime->hwnd);
    if (himc == NULL) {
        return;
    }
    ime->lastCaret = pixel;

    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos.x = pixel.left;
    composition.ptCurrentPos.y = pixel.top;
    ImmSetCompositionWindow(himc, &composition);

    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos.x = pixel.left;
    candidate.ptCurrentPos.y = pixel.bottom;
    candidate.rcArea = pixel;
    ImmSetCandidateWindow(himc, &candidate);
    ImmReleaseContext(ime->hwnd, himc);
}

bool ImeInput_ReadComposition(const ImeInput *ime, WStr *text, int *cursor)
{
    HIMC himc;

    *cursor = 0;
    if (!ReadString(ime->hwnd, GCS_COMPSTR, text)) {
        return false;
    }
    himc = ImmGetContext(ime->hwnd);
    if (himc != NULL) {
        *cursor = (int)(ImmGetCompositionStringW(himc, GCS_CURSORPOS, NULL, 0) & 0xFFFF);
        ImmReleaseContext(ime->hwnd, himc);
    }
    if (*cursor > text->len) {
        *cursor = text->len;
    }
    return true;
}

bool ImeInput_ReadResult(const ImeInput *ime, WStr *text)
{
    return ReadString(ime->hwnd, GCS_RESULTSTR, text);
}
