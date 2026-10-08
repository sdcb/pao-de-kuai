#pragma once

/*
 * A sprite sheet with its pre-downsampled copies.
 *
 * Pure C, and still header-only: the whole class was a std::vector of at most three levels with
 * small accessors, so it is a fixed array plus static-inline functions.  That also removes the
 * last heap allocation from the atlas path.
 *
 * The ComPtr members became raw interface pointers that this struct owns: SetBitmap and AddLevel
 * take ownership of the reference they are given, and Reset/SetBitmap release what they replace.
 * graphics/CppCompat.h keeps the `graphics::SpriteAtlas` class shape for the C++ callers, with
 * ComPtr::Detach transferring the reference in -- exactly the move semantics the old by-value
 * ComPtr parameters had.
 */

#include "graphics/Com.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct PDK_ID2D1Bitmap PDK_ID2D1Bitmap;

/* Full size plus the two Fant-downsampled copies the card atlas builds. */
enum { PDK_ATLAS_LEVELS_MAX = 4 };

typedef struct AtlasLevel {
    PDK_ID2D1Bitmap *bitmap;
    float scale;
} AtlasLevel;

typedef struct SpriteAtlas {
    AtlasLevel levels[PDK_ATLAS_LEVELS_MAX];
    int count;
} SpriteAtlas;

static inline void SpriteAtlas_Reset(SpriteAtlas *atlas)
{
    for (int i = 0; i < atlas->count; ++i) {
        PDK_RELEASE(atlas->levels[i].bitmap);
    }
    atlas->count = 0;
}

/* Pre-downsampled copies; scale is relative to the full-size atlas. */
static inline void SpriteAtlas_AddLevel(SpriteAtlas *atlas, PDK_ID2D1Bitmap *bitmap, float scale)
{
    if (bitmap == NULL) {
        return;
    }
    if (atlas->count >= PDK_ATLAS_LEVELS_MAX) {
        /* Cannot store it, and the caller has handed ownership over, so drop the reference
         * rather than leak it. */
        PDK_RELEASE_VALUE(bitmap);
        return;
    }
    atlas->levels[atlas->count].bitmap = bitmap;
    atlas->levels[atlas->count].scale = scale;
    atlas->count++;
}

static inline void SpriteAtlas_SetBitmap(SpriteAtlas *atlas, PDK_ID2D1Bitmap *bitmap)
{
    SpriteAtlas_Reset(atlas);
    SpriteAtlas_AddLevel(atlas, bitmap, 1.0f);
}

static inline bool SpriteAtlas_Loaded(const SpriteAtlas *atlas)
{
    return atlas->count > 0;
}

static inline PDK_ID2D1Bitmap *SpriteAtlas_Bitmap(const SpriteAtlas *atlas)
{
    return atlas->count > 0 ? atlas->levels[0].bitmap : NULL;
}

/* Smallest level that still has at least the requested pixel density. */
static inline PDK_ID2D1Bitmap *SpriteAtlas_BitmapFor(const SpriteAtlas *atlas,
                                                     float requestedScale, float *levelScale)
{
    const AtlasLevel *best = atlas->count > 0 ? &atlas->levels[0] : NULL;

    for (int i = 0; i < atlas->count; ++i) {
        if (atlas->levels[i].scale >= requestedScale && atlas->levels[i].scale < best->scale) {
            best = &atlas->levels[i];
        }
    }
    *levelScale = best != NULL ? best->scale : 1.0f;
    return best != NULL ? best->bitmap : NULL;
}

#ifdef __cplusplus
}
#endif
