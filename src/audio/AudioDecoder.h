#pragma once

#include <cstdint>
#include <span>
#include <vector>

#include <xaudio2.h>

namespace pdk::audio {

struct AudioData {
    WAVEFORMATEX format{};
    std::vector<std::uint8_t> pcm;
};

bool DecodeMp3ToPcm(std::span<const std::uint8_t> bytes, AudioData& out);

} // namespace pdk::audio
