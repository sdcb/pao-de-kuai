#include "app/App.h"
#include "app/Window.h"
#include "core/Timer.h"
#include "graphics/ComPtr.h"

#include <objbase.h>
#include <propidl.h>
#include <wincodec.h>
#include <windows.h>

#include <algorithm>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

struct Args {
    std::string scene{"start"};
    std::string overlay;
    std::string mock;
    std::string screenshot;
    float quality{0.75f};
};

Args ParseArgs(int argc, char** argv) {
    Args args;
    for (int i = 1; i < argc; ++i) {
        const std::string key = argv[i];
        auto next = [&]() -> std::string {
            if (i + 1 < argc) {
                return argv[++i];
            }
            return {};
        };
        if (key == "--scene") {
            args.scene = next();
        } else if (key == "--overlay") {
            args.overlay = next();
        } else if (key == "--mock") {
            args.mock = next();
        } else if (key == "--screenshot") {
            args.screenshot = next();
        } else if (key == "--quality") {
            args.quality = std::clamp(std::strtof(next().c_str(), nullptr) / 100.0f, 0.01f, 1.0f);
        }
    }
    return args;
}

bool SaveJpeg(IWICBitmapSource* source, const std::wstring& path, float quality) {
    pdk::graphics::ComPtr<IWICImagingFactory> wic;
    if (!source || FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.ReleaseAndGetAddressOf())))) {
        return false;
    }
    UINT width = 0;
    UINT height = 0;
    source->GetSize(&width, &height);
    bool ok = false;
    pdk::graphics::ComPtr<IWICFormatConverter> converter;
    pdk::graphics::ComPtr<IWICStream> stream;
    pdk::graphics::ComPtr<IWICBitmapEncoder> encoder;
    pdk::graphics::ComPtr<IWICBitmapFrameEncode> frame;
    IPropertyBag2* rawBag = nullptr;
    if (SUCCEEDED(wic->CreateFormatConverter(converter.ReleaseAndGetAddressOf())) &&
        SUCCEEDED(converter->Initialize(source, GUID_WICPixelFormat24bppBGR, WICBitmapDitherTypeNone, nullptr, 0.0, WICBitmapPaletteTypeMedianCut)) &&
        SUCCEEDED(wic->CreateStream(stream.ReleaseAndGetAddressOf())) &&
        SUCCEEDED(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE)) &&
        SUCCEEDED(wic->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, encoder.ReleaseAndGetAddressOf())) &&
        SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)) &&
        SUCCEEDED(encoder->CreateNewFrame(frame.ReleaseAndGetAddressOf(), &rawBag))) {
        pdk::graphics::ComPtr<IPropertyBag2> bag(rawBag);
        PROPBAG2 option{};
        option.pstrName = const_cast<LPOLESTR>(L"ImageQuality");
        VARIANT value{};
        VariantInit(&value);
        value.vt = VT_R4;
        value.fltVal = quality;
        if (bag) {
            bag->Write(1, &option, &value);
        }
        if (SUCCEEDED(frame->Initialize(bag.Get())) &&
            SUCCEEDED(frame->SetSize(width, height))) {
            WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
            if (SUCCEEDED(frame->SetPixelFormat(&format)) &&
                SUCCEEDED(frame->WriteSource(converter.Get(), nullptr)) &&
                SUCCEEDED(frame->Commit()) &&
                SUCCEEDED(encoder->Commit())) {
                ok = true;
            }
        }
        VariantClear(&value);
    } else if (rawBag) {
        rawBag->Release();
    }
    return ok;
}

bool CaptureWindowJpeg(HWND hwnd, const std::wstring& path, float quality) {
    RECT rc{};
    GetClientRect(hwnd, &rc);
    const int width = rc.right - rc.left;
    const int height = rc.bottom - rc.top;
    if (width <= 0 || height <= 0) {
        return false;
    }

    BITMAPINFO bmi{};
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = width;
    bmi.bmiHeader.biHeight = -height;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    void* pixels = nullptr;
    HDC screen = GetDC(hwnd);
    HDC memory = CreateCompatibleDC(screen);
    HBITMAP bitmap = CreateDIBSection(screen, &bmi, DIB_RGB_COLORS, &pixels, nullptr, 0);
    if (!bitmap || !pixels) {
        if (bitmap) {
            DeleteObject(bitmap);
        }
        DeleteDC(memory);
        ReleaseDC(hwnd, screen);
        return false;
    }
    HGDIOBJ old = SelectObject(memory, bitmap);
    BitBlt(memory, 0, 0, width, height, screen, 0, 0, SRCCOPY);

    bool ok = false;
    pdk::graphics::ComPtr<IWICImagingFactory> wic;
    if (SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(wic.ReleaseAndGetAddressOf())))) {
        pdk::graphics::ComPtr<IWICBitmap> wicBitmap;
        const UINT stride = static_cast<UINT>(width * 4);
        const UINT bufferSize = static_cast<UINT>(stride * height);
        if (SUCCEEDED(wic->CreateBitmapFromMemory(width, height, GUID_WICPixelFormat32bppBGRA, stride, bufferSize, static_cast<BYTE*>(pixels), wicBitmap.ReleaseAndGetAddressOf()))) {
            ok = SaveJpeg(wicBitmap.Get(), path, quality);
        }
    }

    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(hwnd, screen);
    return ok;
}

} // namespace

int main(int argc, char** argv) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const Args args = ParseArgs(argc, argv);

    pdk::app::App app;
    pdk::app::Window window;
    if (!window.Create(app, L"scene_viewer", 1280, 720)) {
        CoUninitialize();
        return 1;
    }
    if (!app.Initialize(window.Hwnd(), true, !args.screenshot.empty())) {
        CoUninitialize();
        return 2;
    }
    app.ShowViewerScene(args.scene, args.overlay, args.mock);
    app.Resize(1280, 720);

    if (args.screenshot.empty()) {
        const int result = window.Run();
        CoUninitialize();
        return result;
    }

    pdk::core::FrameTimer timer;
    MSG msg{};
    // Toasts and talk bubbles expire after 2-3 s, so capture those overlays early.
    const bool transient = args.overlay == "invalid" || args.overlay == "talk";
    int frameCount = args.mock == "deal" ? 45 : (args.mock == "midgame" ? 240 : (transient ? 16 : 175));
    if (args.scene == "loading") {
        frameCount = 2;
    }
    for (int frame = 0; frame < frameCount; ++frame) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (app.ProcessDialogMessage(&msg)) {
                continue;
            }
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        app.Resize(1280, 720);
        app.Update(timer.Tick());
        app.Render();
        Sleep(16);
    }

    // Scenes render offscreen so screenshots work even when the window is occluded;
    // the native settings dialog is still captured from its window.
    const std::wstring path = Utf8ToWide(args.screenshot);
    const bool ok = app.SettingsDialogHwnd()
        ? CaptureWindowJpeg(app.SettingsDialogHwnd(), path, args.quality)
        : SaveJpeg(app.RenderContext().OffscreenBitmap(), path, args.quality);
    app.ConfirmExit();
    CoUninitialize();
    return ok ? 0 : 3;
}
