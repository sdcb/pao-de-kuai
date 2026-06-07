#include "app/Dpi.h"

#include <windows.h>

namespace pdk::app {

void EnableSystemDpiAwareness() {
    SetProcessDPIAware();
}

} // namespace pdk::app
