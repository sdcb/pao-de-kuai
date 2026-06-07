#include "app/App.h"
#include "app/Window.h"

#include <objbase.h>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);

    pdk::app::App app;
    pdk::app::Window window;
    if (!window.Create(app, L"\x6781\x5BA2\x7248\x8DD1\x5F97\x5FEB", 1280, 720)) {
        CoUninitialize();
        return 1;
    }
    if (!app.Initialize(window.Hwnd(), false)) {
        CoUninitialize();
        return 2;
    }
    const int result = window.Run();
    CoUninitialize();
    return result;
}
