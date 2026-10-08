#include "graphics/win_compat.h"

#include "app/Window.h"

#include "app/Dpi.h"
#include "app/WindowChrome.h"
#include "core/Geometry.h"
#include "core/Timer.h"
#include "resources/ResourceIds.h"

#include <string.h>

#include <windowsx.h>

static LRESULT CALLBACK Window_StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
static LRESULT Window_WndProc(Window *window, UINT message, WPARAM wParam, LPARAM lParam);
static void Window_ForwardMouse(Window *window, UINT message, LPARAM lParam);

bool Window_Create(Window *window, App *app, const wchar_t *title, int width, int height)
{
    WNDCLASSEXW wc;
    RECT rect;

    window->app = app;
    EnableSystemDpiAwareness();

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = &Window_StaticWndProc;
    wc.hInstance = GetModuleHandleW(NULL);
    wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
    wc.hIcon = LoadIconW(GetModuleHandleW(NULL), MAKEINTRESOURCEW(IDI_PAO_DE_KUAI));
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = L"PaoDeKuaiWindow";
    RegisterClassExW(&wc);

    rect.left = 0;
    rect.top = 0;
    rect.right = width;
    rect.bottom = height;
    AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
    window->hwnd = CreateWindowExW(0, wc.lpszClassName, title, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT,
                                   CW_USEDEFAULT, rect.right - rect.left, rect.bottom - rect.top,
                                   NULL, NULL, wc.hInstance, window);

    if (window->hwnd == NULL) {
        return false;
    }
    ApplyWindowChrome(window->hwnd);
    ShowWindow(window->hwnd, SW_SHOW);
    UpdateWindow(window->hwnd);
    return true;
}

int Window_Run(Window *window)
{
    FrameTimer timer;
    MSG msg;

    FrameTimer_Init(&timer);
    memset(&msg, 0, sizeof(msg));
    while (!App_ShouldQuit(window->app)) {
        while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) {
                return (int)msg.wParam;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        App_Update(window->app, FrameTimer_Tick(&timer));
        App_Render(window->app);
        Sleep(1);
    }
    return 0;
}

HWND Window_Hwnd(const Window *window)
{
    return window->hwnd;
}

static LRESULT CALLBACK Window_StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    Window *window = NULL;

    if (message == WM_NCCREATE) {
        CREATESTRUCTW *create = (CREATESTRUCTW *)lParam;

        window = (Window *)create->lpCreateParams;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, (LONG_PTR)window);
        window->hwnd = hwnd;
    } else {
        window = (Window *)GetWindowLongPtrW(hwnd, GWLP_USERDATA);
    }
    if (window != NULL) {
        return Window_WndProc(window, message, wParam, lParam);
    }
    return DefWindowProcW(hwnd, message, wParam, lParam);
}

static LRESULT Window_WndProc(Window *window, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message) {
    case WM_CREATE:
        return 0;
    case WM_GETMINMAXINFO: {
        MINMAXINFO *info = (MINMAXINFO *)lParam;
        RECT rect;

        rect.left = 0;
        rect.top = 0;
        rect.right = 1280;
        rect.bottom = 720;
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);
        info->ptMinTrackSize.x = rect.right - rect.left;
        info->ptMinTrackSize.y = rect.bottom - rect.top;
        return 0;
    }
    case WM_SIZE:
        if (window->app != NULL) {
            App_Resize(window->app, LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    case WM_MOUSEMOVE:
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
        Window_ForwardMouse(window, message, lParam);
        return 0;
    case WM_KEYDOWN:
        if (window->app != NULL) {
            const bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            KeyEvent key;

            key.key = (unsigned)wParam;
            key.ctrl = ctrl;
            key.shift = shift;
            if (App_OnKeyDown(window->app, &key)) {
                return 0;
            }
        }
        break;
    case WM_CHAR:
        /* Control characters arrive through WM_KEYDOWN instead. */
        if (window->app != NULL && wParam >= 0x20 && wParam != 0x7F) {
            const wchar_t text[2] = {(wchar_t)wParam, L'\0'};

            App_OnText(window->app, text);
        }
        return 0;
    case WM_IME_SETCONTEXT:
    case WM_IME_STARTCOMPOSITION:
    case WM_IME_COMPOSITION:
    case WM_IME_ENDCOMPOSITION:
        if (window->app != NULL) {
            /* The IME handler writes through the lParam reference (it strips the composition
             * window flag), so it gets a local copy and the caller keeps the window's own. */
            LPARAM imeParam = lParam;

            if (App_HandleImeMessage(window->app, message, wParam, &imeParam)) {
                return 0;
            }
            lParam = imeParam;
        }
        break;
    case WM_CLOSE:
        if (window->app != NULL) {
            App_RequestClose(window->app);
            return 0;
        }
        break;
    default:
        break;
    }
    return DefWindowProcW(window->hwnd, message, wParam, lParam);
}

static void Window_ForwardMouse(Window *window, UINT message, LPARAM lParam)
{
    float x;
    float y;
    ViewTransform view;
    Point logical;

    if (window->app == NULL) {
        return;
    }
    x = (float)GET_X_LPARAM(lParam);
    y = (float)GET_Y_LPARAM(lParam);
    view = RenderContext_View(App_RenderContext(window->app));
    logical = ToLogical(Point_Make(x, y), &view);
    if (message == WM_MOUSEMOVE) {
        App_OnMouseMove(window->app, logical.x, logical.y);
    } else if (message == WM_LBUTTONDOWN) {
        SetCapture(window->hwnd);
        App_OnMouseDown(window->app, logical.x, logical.y);
    } else if (message == WM_LBUTTONUP) {
        ReleaseCapture();
        App_OnMouseUp(window->app, logical.x, logical.y);
    }
}
