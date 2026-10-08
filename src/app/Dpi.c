#include "app/Dpi.h"

#include <windows.h>

void EnableSystemDpiAwareness(void)
{
    SetProcessDPIAware();
}
