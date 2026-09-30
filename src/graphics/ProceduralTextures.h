#pragma once

#include "graphics/ComPtr.h"

#include <d2d1.h>

namespace pdk::graphics {

// Device-dependent bitmaps generated on the CPU so soft shadows and felt grain
// work with a plain Direct2D 1.0 render target (no effects, Win8 compatible).
class ProceduralTextures {
public:
    // The shadow mask is a blurred rounded rect: ShadowPad px of blur margin on each
    // side of a ShadowCore px square, blurred with a Gaussian of ShadowSigma px.
    static constexpr int ShadowPad = 32;
    static constexpr int ShadowCore = 64;
    static constexpr int ShadowSize = ShadowPad * 2 + ShadowCore;
    static constexpr float ShadowSigma = 10.0f;

    ID2D1Bitmap* Shadow(ID2D1RenderTarget* target);
    ID2D1BitmapBrush* Felt(ID2D1RenderTarget* target);
    void Reset();

private:
    ComPtr<ID2D1Bitmap> shadow_;
    ComPtr<ID2D1Bitmap> felt_;
    ComPtr<ID2D1BitmapBrush> feltBrush_;
};

} // namespace pdk::graphics
