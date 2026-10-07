#include "app/WindowChrome.h"

#include <dwmapi.h>

/* Values from newer SDK headers; defined locally so older SDKs still compile. */
#define PDK_DWM_USE_IMMERSIVE_DARK_MODE 20
#define PDK_DWM_BORDER_COLOR 34
#define PDK_DWM_CAPTION_COLOR 35
#define PDK_DWM_TEXT_COLOR 36

void ApplyWindowChrome(HWND hwnd)
{
    BOOL dark;
    COLORREF caption;
    COLORREF text;
    COLORREF border;

    if (hwnd == NULL) {
        return;
    }
    dark = TRUE;
    DwmSetWindowAttribute(hwnd, PDK_DWM_USE_IMMERSIVE_DARK_MODE, &dark, sizeof(dark));
    caption = RGB(0x0A, 0x1F, 0x19);
    text = RGB(0xE8, 0xD6, 0xA8);
    border = RGB(0x3B, 0x33, 0x20);
    DwmSetWindowAttribute(hwnd, PDK_DWM_CAPTION_COLOR, &caption, sizeof(caption));
    DwmSetWindowAttribute(hwnd, PDK_DWM_TEXT_COLOR, &text, sizeof(text));
    DwmSetWindowAttribute(hwnd, PDK_DWM_BORDER_COLOR, &border, sizeof(border));
}
