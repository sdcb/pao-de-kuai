#include "audio/WavLoader.h"

#include <cstring>

namespace pdk::audio {
namespace {

std::uint32_t ReadU32(const std::uint8_t* p) {
    return static_cast<std::uint32_t>(p[0]) |
        (static_cast<std::uint32_t>(p[1]) << 8) |
        (static_cast<std::uint32_t>(p[2]) << 16) |
        (static_cast<std::uint32_t>(p[3]) << 24);
}

std::uint16_t ReadU16(const std::uint8_t* p) {
    return static_cast<std::uint16_t>(p[0]) |
        (static_cast<std::uint16_t>(p[1]) << 8);
}

bool FourCc(const std::uint8_t* p, const char* id) {
    return std::memcmp(p, id, 4) == 0;
}

} // namespace

bool ParseWav(std::span<const std::uint8_t> bytes, WavData& out) {
    if (bytes.size() < 44 || !FourCc(bytes.data(), "RIFF") || !FourCc(bytes.data() + 8, "WAVE")) {
        return false;
    }

    bool hasFmt = false;
    bool hasData = false;
    std::size_t offset = 12;
    while (offset + 8 <= bytes.size()) {
        const std::uint8_t* chunk = bytes.data() + offset;
        const std::uint32_t chunkSize = ReadU32(chunk + 4);
        const std::size_t dataOffset = offset + 8;
        if (dataOffset + chunkSize > bytes.size()) {
            return false;
        }

        if (FourCc(chunk, "fmt ")) {
            if (chunkSize < 16) {
                return false;
            }
            out.format.wFormatTag = ReadU16(bytes.data() + dataOffset);
            out.format.nChannels = ReadU16(bytes.data() + dataOffset + 2);
            out.format.nSamplesPerSec = ReadU32(bytes.data() + dataOffset + 4);
            out.format.nAvgBytesPerSec = ReadU32(bytes.data() + dataOffset + 8);
            out.format.nBlockAlign = ReadU16(bytes.data() + dataOffset + 12);
            out.format.wBitsPerSample = ReadU16(bytes.data() + dataOffset + 14);
            out.format.cbSize = 0;
            hasFmt = true;
        } else if (FourCc(chunk, "data")) {
            out.pcm.assign(bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset),
                bytes.begin() + static_cast<std::ptrdiff_t>(dataOffset + chunkSize));
            hasData = true;
        }

        offset = dataOffset + chunkSize + (chunkSize % 2);
    }

    return hasFmt && hasData && !out.pcm.empty();
}

} // namespace pdk::audio
