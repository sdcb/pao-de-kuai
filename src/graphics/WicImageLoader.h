#pragma once

#include "graphics/ComPtr.h"

#include <cstdint>
#include <span>

#include <d2d1.h>
#include <wincodec.h>

namespace pdk::graphics {

ComPtr<ID2D1Bitmap> LoadBitmapFromMemory(
    ID2D1RenderTarget* target,
    IWICImagingFactory* wicFactory,
    std::span<const std::uint8_t> bytes);

} // namespace pdk::graphics
