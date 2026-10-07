#pragma once

/*
 * Pure-C module.  The header keeps `extern "C"` so the C++ translation units
 * that still exist during the port (tests, scene_viewer) can call it while
 * src/ is converted file by file.
 */

#ifdef __cplusplus
extern "C" {
#endif

void EnableSystemDpiAwareness(void);

#ifdef __cplusplus
}
#endif
