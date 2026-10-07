#include "app/ImeInput.h"

#include "ui/Anim.h"

#include <imm.h>

namespace pdk::app {
namespace {

bool ReadString(HWND hwnd, DWORD index, std::wstring& text) {
    text.clear();
    HIMC himc = ImmGetContext(hwnd);
    if (!himc) {
        return false;
    }
    const LONG bytes = ImmGetCompositionStringW(himc, index, nullptr, 0);
    if (bytes > 0) {
        text.resize(static_cast<std::size_t>(bytes) / sizeof(wchar_t));
        ImmGetCompositionStringW(himc, index, text.data(), static_cast<DWORD>(bytes));
    }
    ImmReleaseContext(hwnd, himc);
    return bytes >= 0;
}

} // namespace

void ImeInput::Attach(HWND hwnd) {
    hwnd_ = hwnd;
    enabled_ = true;
    SetEnabled(false);
}

void ImeInput::SetEnabled(bool enabled) {
    if (!hwnd_ || enabled == enabled_) {
        return;
    }
    enabled_ = enabled;
    ImmAssociateContextEx(hwnd_, nullptr, enabled ? IACE_DEFAULT : 0);
    lastCaret_ = {-1, -1, -1, -1};
}

void ImeInput::SetCaret(const Rect& caret, const ViewTransform& view) {
    if (!hwnd_ || !enabled_) {
        return;
    }
    const RECT pixel{
        RoundToInt(caret.x * view.scale + view.offsetX),
        RoundToInt(caret.y * view.scale + view.offsetY),
        RoundToInt((caret.x + caret.width) * view.scale + view.offsetX),
        RoundToInt((caret.y + caret.height) * view.scale + view.offsetY)
    };
    if (pixel.left == lastCaret_.left && pixel.top == lastCaret_.top && pixel.bottom == lastCaret_.bottom) {
        return;
    }
    HIMC himc = ImmGetContext(hwnd_);
    if (!himc) {
        return;
    }
    lastCaret_ = pixel;

    COMPOSITIONFORM composition{};
    composition.dwStyle = CFS_POINT;
    composition.ptCurrentPos = {pixel.left, pixel.top};
    ImmSetCompositionWindow(himc, &composition);

    CANDIDATEFORM candidate{};
    candidate.dwIndex = 0;
    candidate.dwStyle = CFS_EXCLUDE;
    candidate.ptCurrentPos = {pixel.left, pixel.bottom};
    candidate.rcArea = pixel;
    ImmSetCandidateWindow(himc, &candidate);
    ImmReleaseContext(hwnd_, himc);
}

bool ImeInput::ReadComposition(std::wstring& text, int& cursor) const {
    cursor = 0;
    if (!ReadString(hwnd_, GCS_COMPSTR, text)) {
        return false;
    }
    HIMC himc = ImmGetContext(hwnd_);
    if (himc) {
        cursor = ImmGetCompositionStringW(himc, GCS_CURSORPOS, nullptr, 0) & 0xFFFF;
        ImmReleaseContext(hwnd_, himc);
    }
    if (cursor > static_cast<int>(text.size())) {
        cursor = static_cast<int>(text.size());
    }
    return true;
}

bool ImeInput::ReadResult(std::wstring& text) const {
    return ReadString(hwnd_, GCS_RESULTSTR, text);
}

} // namespace pdk::app
