#include "app/WindowChrome.h"

#include <dwmapi.h>

namespace pdk::app {
namespace {

// Values from newer SDK headers; defined locally so older SDKs still compile.
constexpr DWORD DwmUseImmersiveDarkMode = 20;
constexpr DWORD DwmBorderColor = 34;
constexpr DWORD DwmCaptionColor = 35;
constexpr DWORD DwmTextColor = 36;

} // namespace

void ApplyWindowChrome(HWND hwnd) {
    if (!hwnd) {
        return;
    }
    const BOOL dark = TRUE;
    DwmSetWindowAttribute(hwnd, DwmUseImmersiveDarkMode, &dark, sizeof(dark));
    const COLORREF caption = RGB(0x0A, 0x1F, 0x19);
    const COLORREF text = RGB(0xE8, 0xD6, 0xA8);
    const COLORREF border = RGB(0x3B, 0x33, 0x20);
    DwmSetWindowAttribute(hwnd, DwmCaptionColor, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, DwmTextColor, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, DwmBorderColor, &border, sizeof(border));
}

} // namespace pdk::app
