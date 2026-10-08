#pragma once

/*
 * Device-dependent bitmaps generated on the CPU so soft shadows and felt grain work with a
 * plain Direct2D 1.0 render target (no effects).
 *
 * Pure C.  This is the first real consumer of the flat Direct2D shim (graphics/d2d_c.h): the
 * interface pointers are PDK_* mirrors of the SDK types and calls go through PDK_CALL, which
 * is how C code drives Direct2D at all -- neither MSVC nor MinGW C mode can call the real C++
 * interface declarations.
 *
 * The mirrors are only *forward declared* here and the full shim is included by
 * ProceduralTextures.c.  That is deliberate: graphics/d2d_c.h pulls graphics/dwrite_c.h,
 * whose vendored DWRITE_* types collide with the real <dwrite.h> that C++ translation units
 * include through <d2d1.h>.  Keeping the shim out of the header keeps every C++ consumer
 * (graphics/D2DContext.h and friends) on the real SDK headers, which is the invariant the
 * port relies on.  Repeating these typedefs is legal in both languages.
 *
 * The old class held three ComPtr members; here they are raw interface pointers owned by the
 * struct, created lazily and dropped by ProceduralTextures_Reset.  ProceduralTextures_Init
 * must be called before use.
 */

#include <stdbool.h>
#include <stdint.h>

typedef struct PDK_ID2D1Bitmap PDK_ID2D1Bitmap;
typedef struct PDK_ID2D1RenderTarget PDK_ID2D1RenderTarget;
typedef struct PDK_ID2D1BitmapBrush PDK_ID2D1BitmapBrush;

#ifdef __cplusplus
extern "C" {
#endif

/* The shadow mask is a blurred rounded rect: ShadowPad px of blur margin on each side of a
 * ShadowCore px square, blurred with a Gaussian of ShadowSigma px. */
enum { PDK_SHADOW_PAD = 32 };
enum { PDK_SHADOW_CORE = 64 };
enum { PDK_SHADOW_SIZE = PDK_SHADOW_PAD * 2 + PDK_SHADOW_CORE };
#define PDK_SHADOW_SIGMA 10.0f

typedef struct ProceduralTextures {
    PDK_ID2D1Bitmap *shadow;
    PDK_ID2D1Bitmap *felt;
    PDK_ID2D1BitmapBrush *feltBrush;
} ProceduralTextures;

void ProceduralTextures_Init(ProceduralTextures *textures);
/* Both return a borrowed reference owned by the struct, or NULL when there is no target or
 * the bitmap could not be created. */
PDK_ID2D1Bitmap *ProceduralTextures_Shadow(ProceduralTextures *textures,
                                          PDK_ID2D1RenderTarget *target);
PDK_ID2D1BitmapBrush *ProceduralTextures_Felt(ProceduralTextures *textures,
                                             PDK_ID2D1RenderTarget *target);
void ProceduralTextures_Reset(ProceduralTextures *textures);

#ifdef __cplusplus
}
#endif