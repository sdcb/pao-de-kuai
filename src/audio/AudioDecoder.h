#pragma once

#include <cstdint>
#include <span>
#include <vector>

namespace pdk::audio {

// Mono float samples at the source sample rate.
struct AudioData {
    std::uint32_t sampleRate{44100};
    std::vector<float> samples;
};

bool DecodeMp3ToPcm(std::span<const std::uint8_t> bytes, AudioData& out);

} // namespace pdk::audio
