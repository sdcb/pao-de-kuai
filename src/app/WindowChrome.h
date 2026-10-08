#pragma once

/*
 * Pure-C module.  The header keeps `extern "C"` so the C++ translation units
 * that still exist during the port (tests, scene_viewer) can call it while
 * src/ is converted file by file.
 */

#include <windows.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Dark, ink-green caption matching the in-game palette.  Attributes the
 * running Windows version does not know about are ignored. */
void ApplyWindowChrome(HWND hwnd);

#ifdef __cplusplus
}
#endif
