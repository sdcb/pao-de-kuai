#pragma once

#include <windows.h>

namespace pdk::app {

// Dark, ink-green caption matching the in-game palette. Attributes the running
// Windows version does not know about are ignored, so this is safe on Win8.
void ApplyWindowChrome(HWND hwnd);

} // namespace pdk::app
