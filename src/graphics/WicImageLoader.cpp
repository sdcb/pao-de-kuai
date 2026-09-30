#include "graphics/WicImageLoader.h"

#include <objbase.h>

namespace pdk::graphics {

ComPtr<ID2D1Bitmap> LoadBitmapFromMemory(
    ID2D1RenderTarget* target,
    IWICImagingFactory* wicFactory,
    std::span<const std::uint8_t> bytes,
    float scale) {
    if (!target || !wicFactory || bytes.empty()) {
        return {};
    }

    ComPtr<IWICStream> stream;
    if (FAILED(wicFactory->CreateStream(stream.ReleaseAndGetAddressOf()))) {
        return {};
    }
    if (FAILED(stream->InitializeFromMemory(
            const_cast<BYTE*>(reinterpret_cast<const BYTE*>(bytes.data())),
            static_cast<DWORD>(bytes.size())))) {
        return {};
    }

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(wicFactory->CreateDecoderFromStream(
            stream.Get(),
            nullptr,
            WICDecodeMetadataCacheOnLoad,
            decoder.ReleaseAndGetAddressOf()))) {
        return {};
    }

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, frame.ReleaseAndGetAddressOf()))) {
        return {};
    }

    ComPtr<IWICBitmapSource> source;
    source.Attach(frame.Get());
    source->AddRef();
    if (scale < 1.0f) {
        UINT width = 0;
        UINT height = 0;
        frame->GetSize(&width, &height);
        ComPtr<IWICBitmapScaler> scaler;
        if (SUCCEEDED(wicFactory->CreateBitmapScaler(scaler.ReleaseAndGetAddressOf())) &&
            SUCCEEDED(scaler->Initialize(
                frame.Get(),
                static_cast<UINT>(static_cast<float>(width) * scale + 0.5f),
                static_cast<UINT>(static_cast<float>(height) * scale + 0.5f),
                WICBitmapInterpolationModeFant))) {
            source.Attach(scaler.Detach());
        }
    }

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(converter.ReleaseAndGetAddressOf()))) {
        return {};
    }
    if (FAILED(converter->Initialize(
            source.Get(),
            GUID_WICPixelFormat32bppPBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeMedianCut))) {
        return {};
    }

    ComPtr<ID2D1Bitmap> bitmap;
    if (FAILED(target->CreateBitmapFromWicBitmap(converter.Get(), nullptr, bitmap.ReleaseAndGetAddressOf()))) {
        return {};
    }
    return bitmap;
}

} // namespace pdk::graphics
