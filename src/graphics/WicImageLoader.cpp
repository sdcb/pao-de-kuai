#include "graphics/WicImageLoader.h"

#include <objbase.h>

namespace pdk::graphics {

ComPtr<ID2D1Bitmap> LoadBitmapFromMemory(
    ID2D1RenderTarget* target,
    IWICImagingFactory* wicFactory,
    std::span<const std::uint8_t> bytes) {
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

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(wicFactory->CreateFormatConverter(converter.ReleaseAndGetAddressOf()))) {
        return {};
    }
    if (FAILED(converter->Initialize(
            frame.Get(),
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
