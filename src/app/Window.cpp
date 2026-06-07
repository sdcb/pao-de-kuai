#include "app/Window.h"

#include "app/Dpi.h"
#include "core/Geometry.h"
#include "core/Timer.h"
#include "resources/ResourceIds.h"

#include <algorithm>
#include <windowsx.h>

namespace pdk::app {

bool Window::Create(App& app, const wchar_t* title, int width, int height) {
    app_ = &app;
    EnableSystemDpiAwareness();

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window::StaticWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hIcon = LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(IDI_PAO_DE_KUAI));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"PaoDeKuaiWindow";
    RegisterClassExW(&wc);

    RECT rect{0, 0, width, height};
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    hwnd_ = CreateWindowExW(
        0,
        wc.lpszClassName,
        title,
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT,
        CW_USEDEFAULT,
        rect.right - rect.left,
        rect.bottom - rect.top,
        nullptr,
        nullptr,
        wc.hInstance,
        this);

    if (!hwnd_) {
        return false;
    }
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

int Window::Run() {
    core::FrameTimer timer;
    MSG msg{};
    while (!app_->ShouldQuit()) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return static_cast<int>(msg.wParam);
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        const float dt = timer.Tick();
        app_->Update(dt);
        app_->Render();
        Sleep(1);
    }
    return 0;
}

LRESULT CALLBACK Window::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    Window* window = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        window = static_cast<Window*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(window));
        window->hwnd_ = hwnd;
    } else {
        window = reinterpret_cast<Window*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    if (window) {
        return window->WndProc(message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT Window::WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_CREATE:
        return 0;
    case WM_GETMINMAXINFO: {
        auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
        RECT rect{0, 0, 1280, 720};
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        info->ptMinTrackSize.x = rect.right - rect.left;
        info->ptMinTrackSize.y = rect.bottom - rect.top;
        return 0;
    }
    case WM_SIZE:
        if (app_) {
            app_->Resize(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        ForwardMouse(message, lParam);
        return 0;
    case WM_CLOSE:
        if (app_) {
            app_->RequestClose();
            return 0;
        }
        break;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

void Window::ForwardMouse(UINT message, LPARAM lParam) {
    if (!app_) {
        return;
    }
    const float x = static_cast<float>(GET_X_LPARAM(lParam));
    const float y = static_cast<float>(GET_Y_LPARAM(lParam));
    const core::Point logical = core::ToLogical({x, y}, app_->RenderContext().ViewTransform());
    if (message == WM_MOUSEMOVE) {
        app_->OnMouseMove(logical.x, logical.y);
    } else if (message == WM_LBUTTONDOWN) {
        SetCapture(hwnd_);
        app_->OnMouseDown(logical.x, logical.y);
    } else if (message == WM_LBUTTONUP) {
        ReleaseCapture();
        app_->OnMouseUp(logical.x, logical.y);
    }
}

} // namespace pdk::app
