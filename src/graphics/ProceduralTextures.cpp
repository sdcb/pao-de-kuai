#include "graphics/ProceduralTextures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pdk::graphics {
namespace {

constexpr int FeltSize = 128;
constexpr float ShadowCornerRadius = 6.0f;

std::vector<float> GaussianKernel(float sigma) {
    const int radius = static_cast<int>(std::ceil(sigma * 3.0f));
    std::vector<float> kernel(static_cast<std::size_t>(radius * 2 + 1));
    float sum = 0.0f;
    for (int i = -radius; i <= radius; ++i) {
        const float value = std::exp(-(static_cast<float>(i * i)) / (2.0f * sigma * sigma));
        kernel[static_cast<std::size_t>(i + radius)] = value;
        sum += value;
    }
    for (float& value : kernel) {
        value /= sum;
    }
    return kernel;
}

void BlurAxis(const std::vector<float>& in, std::vector<float>& out, int size, const std::vector<float>& kernel, bool horizontal) {
    const int radius = static_cast<int>(kernel.size() / 2);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float acc = 0.0f;
            for (int k = -radius; k <= radius; ++k) {
                const int sx = horizontal ? x + k : x;
                const int sy = horizontal ? y : y + k;
                if (sx < 0 || sy < 0 || sx >= size || sy >= size) {
                    continue;
                }
                acc += in[static_cast<std::size_t>(sy * size + sx)] * kernel[static_cast<std::size_t>(k + radius)];
            }
            out[static_cast<std::size_t>(y * size + x)] = acc;
        }
    }
}

float RoundedRectCoverage(float px, float py, float left, float top, float right, float bottom, float radius) {
    const float cx = std::clamp(px, left + radius, right - radius);
    const float cy = std::clamp(py, top + radius, bottom - radius);
    const float dx = px - cx;
    const float dy = py - cy;
    const float distance = std::sqrt(dx * dx + dy * dy) - radius;
    return std::clamp(0.5f - distance, 0.0f, 1.0f);
}

ComPtr<ID2D1Bitmap> CreatePbgraBitmap(ID2D1RenderTarget* target, int size, const std::vector<std::uint32_t>& pixels) {
    ComPtr<ID2D1Bitmap> bitmap;
    const D2D1_BITMAP_PROPERTIES props = D2D1::BitmapProperties(
        D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_PREMULTIPLIED));
    target->CreateBitmap(
        D2D1::SizeU(static_cast<UINT32>(size), static_cast<UINT32>(size)),
        pixels.data(),
        static_cast<UINT32>(size * 4),
        props,
        bitmap.ReleaseAndGetAddressOf());
    return bitmap;
}

} // namespace

ID2D1Bitmap* ProceduralTextures::Shadow(ID2D1RenderTarget* target) {
    if (shadow_ || !target) {
        return shadow_.Get();
    }
    constexpr int size = ShadowSize;
    std::vector<float> mask(static_cast<std::size_t>(size * size));
    const float lo = static_cast<float>(ShadowPad);
    const float hi = static_cast<float>(ShadowPad + ShadowCore);
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            mask[static_cast<std::size_t>(y * size + x)] =
                RoundedRectCoverage(static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, lo, lo, hi, hi, ShadowCornerRadius);
        }
    }
    const std::vector<float> kernel = GaussianKernel(ShadowSigma);
    std::vector<float> temp(mask.size());
    BlurAxis(mask, temp, size, kernel, true);
    BlurAxis(temp, mask, size, kernel, false);

    std::vector<std::uint32_t> pixels(mask.size());
    for (std::size_t i = 0; i < mask.size(); ++i) {
        const auto alpha = static_cast<std::uint32_t>(std::clamp(mask[i], 0.0f, 1.0f) * 255.0f + 0.5f);
        pixels[i] = alpha << 24;
    }
    shadow_ = CreatePbgraBitmap(target, size, pixels);
    return shadow_.Get();
}

ID2D1BitmapBrush* ProceduralTextures::Felt(ID2D1RenderTarget* target) {
    if (feltBrush_ || !target) {
        return feltBrush_.Get();
    }
    constexpr int size = FeltSize;
    std::uint32_t seed = 0x2468ACEu;
    auto next = [&seed]() {
        seed = seed * 1664525u + 1013904223u;
        return static_cast<float>((seed >> 8) & 0xFFFFu) / 65535.0f * 2.0f - 1.0f;
    };
    std::vector<float> noise(static_cast<std::size_t>(size * size));
    for (float& value : noise) {
        value = next();
    }
    // Mix fine grain with short horizontal fibres, wrapping so the tile repeats seamlessly.
    std::vector<float> felt(noise.size());
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float fibre = 0.0f;
            for (int k = -3; k <= 3; ++k) {
                fibre += noise[static_cast<std::size_t>(y * size + ((x + k + size) % size))];
            }
            const float grain = noise[static_cast<std::size_t>(((y * 7 + 3) % size) * size + ((x * 5 + 11) % size))];
            felt[static_cast<std::size_t>(y * size + x)] = fibre / 7.0f * 0.7f + grain * 0.45f;
        }
    }

    std::vector<std::uint32_t> pixels(felt.size());
    for (std::size_t i = 0; i < felt.size(); ++i) {
        const float v = std::clamp(felt[i], -1.0f, 1.0f);
        const auto alpha = static_cast<std::uint32_t>(std::abs(v) * 0.55f * 255.0f + 0.5f);
        const std::uint32_t channel = v > 0.0f ? alpha : 0u;
        pixels[i] = (alpha << 24) | (channel << 16) | (channel << 8) | channel;
    }
    felt_ = CreatePbgraBitmap(target, size, pixels);
    if (felt_) {
        const D2D1_BITMAP_BRUSH_PROPERTIES props = D2D1::BitmapBrushProperties(
            D2D1_EXTEND_MODE_WRAP, D2D1_EXTEND_MODE_WRAP, D2D1_BITMAP_INTERPOLATION_MODE_LINEAR);
        target->CreateBitmapBrush(felt_.Get(), props, feltBrush_.ReleaseAndGetAddressOf());
    }
    return feltBrush_.Get();
}

void ProceduralTextures::Reset() {
    shadow_.Reset();
    feltBrush_.Reset();
    felt_.Reset();
}

} // namespace pdk::graphics
