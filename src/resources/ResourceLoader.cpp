#include "resources/ResourceLoader.h"

namespace pdk::resources {

std::vector<std::uint8_t> LoadResourceBytes(int resourceId, LPCWSTR type, HMODULE module) {
    if (!module) {
        module = GetModuleHandleW(nullptr);
    }
    HRSRC resource = FindResourceW(module, MAKEINTRESOURCEW(resourceId), type);
    if (!resource) {
        return {};
    }
    HGLOBAL loaded = LoadResource(module, resource);
    if (!loaded) {
        return {};
    }
    const DWORD size = SizeofResource(module, resource);
    const void* data = LockResource(loaded);
    if (!data || size == 0) {
        return {};
    }
    const auto* begin = static_cast<const std::uint8_t*>(data);
    return std::vector<std::uint8_t>(begin, begin + size);
}

} // namespace pdk::resources
