#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * `src/graphics/ProceduralTextures.*` and `WicImageLoader.*` are pure C now (plan.md S5):
 * the interface pointers are PDK_* mirrors of the SDK types and calls go through PDK_CALL.
 *
 * The C++ callers still write `graphics::LoadBitmapFromMemory(...)` with a ComPtr result and
 * `textures_.Shadow(target)` on a member object, so this header reproduces exactly those two
 * shapes on top of the C API.  Both wrappers are type casts around the C call -- the PDK_*
 * mirrors are layout-identical to the SDK interfaces, which tests/shim_layout proves at
 * compile time -- so nothing is copied or converted.
 *
 * It grows as the rest of src/graphics is converted and disappears with the last C++ file
 * under src/.
 */

#include "graphics/Com.h"
#include "graphics/ComPtr.h"
#include "graphics/ProceduralTextures.h"
#include "graphics/WicImageLoader.h"

#include <cstdint>
#include <utility>

#include <d2d1.h>
#include <dwrite.h>

namespace pdk::graphics {

/* Borrows the caller's bitmap/brush; ownership stays with the ProceduralTextures value. */
inline ID2D1Bitmap* ProceduralShadow(ProceduralTextures& textures, ID2D1RenderTarget* target)
{
    return reinterpret_cast<ID2D1Bitmap*>(
        ProceduralTextures_Shadow(&textures, PDK_AS(ID2D1RenderTarget, target)));
}

inline ID2D1BitmapBrush* ProceduralFelt(ProceduralTextures& textures, ID2D1RenderTarget* target)
{
    return reinterpret_cast<ID2D1BitmapBrush*>(
        ProceduralTextures_Felt(&textures, PDK_AS(ID2D1RenderTarget, target)));
}

/* Accepts anything with data()/size() over bytes, which is what the resource loader hands
 * back.  The returned ComPtr owns the new reference the C function produced. */
template <typename ByteRange>
inline ComPtr<ID2D1Bitmap> LoadBitmapFromMemory(ID2D1RenderTarget* target,
                                               IWICImagingFactory* wicFactory,
                                               const ByteRange& bytes, float scale = 1.0f)
{
    ComPtr<ID2D1Bitmap> bitmap;

    bitmap.Attach(reinterpret_cast<ID2D1Bitmap*>(WicImageLoader_LoadBitmapFromMemory(
        PDK_AS(ID2D1RenderTarget, target), wicFactory,
        reinterpret_cast<const std::uint8_t*>(bytes.data()), static_cast<int>(bytes.size()),
        scale)));
    return bitmap;
}

} // namespace pdk::graphics
