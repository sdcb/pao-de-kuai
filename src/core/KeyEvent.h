#pragma once

/*
 * One keyboard event, as the window layer hands it to the app and then to the topmost
 * overlay (src/app/Window.cpp -> App::OnKeyDown -> core::Overlay::OnKeyDown).
 *
 * Pure C, split out of core/Overlay.h because ui/Inputs.h is a C header now and the text
 * editor consumes the same struct.  core/Overlay.h itself is still C++ (the Overlay class is
 * virtual), so it includes this and re-exports `pdk::core::KeyEvent` by `using` the global
 * type -- the same name bridge ui/CppCompat.h and rules/CppCompat.h use.
 *
 * The C name lives at global scope: C has no namespaces, and this is the only KeyEvent in
 * the tree.
 */

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KeyEvent {
    unsigned key;
    bool ctrl;
    bool shift;
} KeyEvent;

#ifdef __cplusplus
}
#endif
