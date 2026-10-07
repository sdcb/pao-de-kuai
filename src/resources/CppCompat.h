#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * `src/resources/` is pure C now (plan.md S4a): LoadResourceBytes fills a
 * ByteBuffer and GetCardAtlasInfo returns a pointer.
 *
 * The C++ callers still expect `resources::LoadResourceBytes(id)` to hand back an
 * owning container and `resources::GetCardAtlasInfo()` to hand back a reference,
 * so this header reproduces exactly that.  The PNG is copied once more here; that
 * copy disappears together with this file.
 */

#include <cstdint>
#include <vector>

#include "resources/CardAtlasData.h"
#include "resources/IconAtlasData.h"
#include "resources/ResourceLoader.h"

namespace pdk::resources {

inline std::vector<std::uint8_t> LoadResourceBytes(int resourceId,
                                                  LPCWSTR type = RT_RCDATA,
                                                  HMODULE module = nullptr)
{
    ByteBuffer raw;
    ::LoadResourceBytes(resourceId, type, module, &raw);
    std::vector<std::uint8_t> out;
    if (raw.data != nullptr && raw.size > 0) {
        out.assign(raw.data, raw.data + raw.size);
    }
    ByteBuffer_Free(&raw);
    return out;
}

using ::CardAtlasInfo;
using ::CardBackSourceRect;
using ::CardSourceRect;

inline const CardAtlasInfo& GetCardAtlasInfo()
{
    return *::GetCardAtlasInfo();
}

} // namespace pdk::resources
