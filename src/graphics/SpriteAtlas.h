#pragma once

#include "graphics/ComPtr.h"

#include <vector>

#include <d2d1.h>

namespace pdk::graphics {

class SpriteAtlas {
public:
    void SetBitmap(ComPtr<ID2D1Bitmap> bitmap) {
        levels_.clear();
        AddLevel(std::move(bitmap), 1.0f);
    }
    // Pre-downsampled copies; scale is relative to the full-size atlas.
    void AddLevel(ComPtr<ID2D1Bitmap> bitmap, float scale) {
        if (bitmap) {
            levels_.push_back({std::move(bitmap), scale});
        }
    }
    ID2D1Bitmap* Bitmap() const { return levels_.empty() ? nullptr : levels_.front().bitmap.Get(); }
    // Smallest level that still has at least the requested pixel density.
    ID2D1Bitmap* BitmapFor(float requestedScale, float& levelScale) const {
        const Level* best = levels_.empty() ? nullptr : &levels_.front();
        for (const Level& level : levels_) {
            if (level.scale >= requestedScale && level.scale < best->scale) {
                best = &level;
            }
        }
        levelScale = best ? best->scale : 1.0f;
        return best ? best->bitmap.Get() : nullptr;
    }
    bool Loaded() const { return !levels_.empty(); }
    void Reset() { levels_.clear(); }

private:
    struct Level {
        ComPtr<ID2D1Bitmap> bitmap;
        float scale{1.0f};
    };
    std::vector<Level> levels_;
};

} // namespace pdk::graphics
