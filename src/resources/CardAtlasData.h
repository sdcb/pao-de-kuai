#pragma once

/*
 * Sprite-sheet layout for the card atlas.
 *
 * Pure C.  The atlas metadata is code, not a runtime JSON file (AGENTS.md
 * "资源与数据策略"), so it lives here as a fixed table.
 */

#include "rules/Card.h"

#include <d2d1.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct CardAtlasInfo {
    int columns;
    int rows;
    int cardWidth;
    int cardHeight;
    int gap;
    /* Safe visible width from the left edge before the next card may cover this
     * card; matching the atlas metadata avoids exposing the large right suit. */
    int mainX;
} CardAtlasInfo;

/* Returns a pointer to a file-scope constant, so nothing is copied or owned. */
const CardAtlasInfo *GetCardAtlasInfo(void);
D2D1_RECT_U CardSourceRect(Card card);
D2D1_RECT_U CardBackSourceRect(void);

#ifdef __cplusplus
}
#endif
