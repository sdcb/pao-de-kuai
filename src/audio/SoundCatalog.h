#pragma once

#include "audio/SoundIds.h"

#include <string_view>
#include <vector>

namespace pdk::audio {

struct SoundCatalogEntry {
    SoundId id;
    int resourceId;
    std::string_view fileName;
    float recommendedVolume;
};

const std::vector<SoundCatalogEntry>& SoundCatalog();

} // namespace pdk::audio
