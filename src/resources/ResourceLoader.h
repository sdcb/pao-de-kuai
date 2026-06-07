#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <windows.h>

namespace pdk::resources {

std::vector<std::uint8_t> LoadResourceBytes(int resourceId, LPCWSTR type = RT_RCDATA, HMODULE module = nullptr);

} // namespace pdk::resources
