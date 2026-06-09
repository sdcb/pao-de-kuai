#include "core/StringUtil.h"

#include <charconv>
#include <cstddef>
#include <system_error>

namespace pdk::core {
namespace {

template <typename Int>
void AppendNumberImpl(std::string& text, Int value) {
    char buffer[32]{};
    const auto [end, ec] = std::to_chars(buffer, buffer + sizeof(buffer), value);
    if (ec == std::errc{}) {
        text.append(buffer, end);
    }
}

} // namespace

void AppendNumber(std::string& text, int value) {
    AppendNumberImpl(text, value);
}

void AppendNumber(std::string& text, unsigned int value) {
    AppendNumberImpl(text, value);
}

void AppendNumber(std::string& text, long value) {
    AppendNumberImpl(text, value);
}

void AppendNumber(std::string& text, unsigned long value) {
    AppendNumberImpl(text, value);
}

void AppendNumber(std::string& text, long long value) {
    AppendNumberImpl(text, value);
}

void AppendNumber(std::string& text, unsigned long long value) {
    AppendNumberImpl(text, value);
}

void AppendPaddedNumber(std::string& text, int value, int width) {
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
