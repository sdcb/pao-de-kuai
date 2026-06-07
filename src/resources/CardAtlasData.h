#pragma once

#include "rules/Card.h"

#include <d2d1.h>

namespace pdk::resources {

struct CardAtlasInfo {
    int columns{13};
    int rows{5};
    int cardWidth{240};
    int cardHeight{336};
    int gap{1};
    // Safe visible width from the left edge before the next card may cover this
    // card; matching the atlas metadata avoids exposing the large right suit.
    int mainX{80};
};

const CardAtlasInfo& GetCardAtlasInfo();
D2D1_RECT_U CardSourceRect(rules::Card card);
D2D1_RECT_U CardBackSourceRect();

} // namespace pdk::resources
