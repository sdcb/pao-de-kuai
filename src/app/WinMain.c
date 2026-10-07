#include "graphics/win_compat.h"

#include "app/App.h"
#include "app/Window.h"

#include <objbase.h>
#include <string.h>

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE previous, PWSTR commandLine, int showCommand)
{
    App app;
    Window window;

    (void)instance;
    (void)previous;
    (void)commandLine;
    (void)showCommand;
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    App_Init(&app);
    memset(&window, 0, sizeof(window));
    if (!Window_Create(&window, &app, L"\x6781\x5BA2\x7248\x8DD1\x5F97\x5FEB", 1280, 720)) {
        CoUninitialize();
        return 1;
    }
    if (!App_Initialize(&app, Window_Hwnd(&window), false, false)) {
        CoUninitialize();
        return 2;
    }
    CoUninitialize();
    return Window_Run(&window);
}
