#pragma once

#include "graphics/ComPtr.h"

#include <d2d1.h>

namespace pdk::graphics {

class SpriteAtlas {
public:
    void SetBitmap(ComPtr<ID2D1Bitmap> bitmap) { bitmap_ = std::move(bitmap); }
    ID2D1Bitmap* Bitmap() const { return bitmap_.Get(); }
    bool Loaded() const { return static_cast<bool>(bitmap_); }
    void Reset() { bitmap_.Reset(); }

private:
    ComPtr<ID2D1Bitmap> bitmap_;
};

} // namespace pdk::graphics
