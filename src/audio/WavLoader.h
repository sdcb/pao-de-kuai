#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <xaudio2.h>

namespace pdk::audio {

struct WavData {
    WAVEFORMATEX format{};
    std::vector<std::uint8_t> pcm;
};

bool ParseWav(std::span<const std::uint8_t> bytes, WavData& out);

} // namespace pdk::audio
