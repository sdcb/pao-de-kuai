#pragma once

/*
 * The Win32 shell: registers the window class, creates the window and pumps messages.
 *
 * Pure C.  It holds `App *` and forwards every message to the App_* entry points; the class
 * methods became static functions taking the Window handle, and the mouse coordinates go through
 * the C RenderContext the same way they always did.
 */

#include "app/App.h"

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Window {
    HWND hwnd;
    App *app;
} Window;

bool Window_Create(Window *window, App *app, const wchar_t *title, int width, int height);
int Window_Run(Window *window);
HWND Window_Hwnd(const Window *window);

#ifdef __cplusplus
}
#endif
