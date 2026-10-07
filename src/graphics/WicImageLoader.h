#pragma once

/*
 * Decodes an encoded image from memory into a Direct2D bitmap.
 *
 * Pure C.  `std::span<const std::uint8_t>` became a pointer plus a count, and the returned
 * ComPtr became a raw pointer with a *new reference* -- the caller owns it and must release
 * it.  The WIC interfaces come from the platform <wincodec.h>, which both toolchains parse in
 * C mode (WIC is declared with the COM macros, not C++ inheritance).
 *
 * The Direct2D mirror is only forward declared here; see ProceduralTextures.h for why the
 * shim must stay out of a header that C++ translation units include.
 */

#include <stdint.h>

#include <wincodec.h>

typedef struct PDK_ID2D1Bitmap PDK_ID2D1Bitmap;
typedef struct PDK_ID2D1RenderTarget PDK_ID2D1RenderTarget;

#ifdef __cplusplus
extern "C" {
#endif

/*
 * scale < 1 downsamples with WIC's Fant filter, which stays sharp where GPU bilinear
 * minification of the full-size atlas would alias.
 *
 * Returns a new reference, or NULL when the input is empty or a step failed.
 */
PDK_ID2D1Bitmap *WicImageLoader_LoadBitmapFromMemory(PDK_ID2D1RenderTarget *target,
                                                     IWICImagingFactory *wicFactory,
                                                     const uint8_t *bytes, int count,
                                                     float scale);

#ifdef __cplusplus
}
#endif