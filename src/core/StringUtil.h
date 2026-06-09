#pragma once

#include <charconv>
#include <cstddef>
#include <string>
#include <system_error>

namespace pdk::core {

template <typename Int>
void AppendNumber(std::string& text, Int value) {
    char buffer[32]{};
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec == std::errc{}) {
        text.append(buffer, end);
    }
}

inline void AppendPaddedNumber(std::string& text, int value, int width) {
    char buffer[32]{};
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec != std::errc{}) {
        return;
    }
    const std::ptrdiff_t length = end - buffer;
    for (int i = static_cast<int>(length); i < width; ++i) {
        text += '0';
    }
    text.append(buffer, end);
}

} // namespace pdk::core
