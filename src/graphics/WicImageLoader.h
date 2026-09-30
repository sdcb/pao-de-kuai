#pragma once

#include "graphics/ComPtr.h"

#include <cstdint>
#include <span>

#include <d2d1.h>
#include <wincodec.h>

namespace pdk::graphics {

// scale < 1 downsamples with WIC's Fant filter, which stays sharp where GPU
// bilinear minification of the full-size atlas would alias.
ComPtr<ID2D1Bitmap> LoadBitmapFromMemory(
    ID2D1RenderTarget* target,
    IWICImagingFactory* wicFactory,
    std::span<const std::uint8_t> bytes,
    float scale = 1.0f);

} // namespace pdk::graphics
