#include "graphics/win_compat.h"

#include "graphics/d2d_c.h"

#include "graphics/ProceduralTextures.h"

#include <math.h>
#include <stdint.h>

enum { PDK_FELT_SIZE = 128 };
enum { PDK_MAX_BLUR_RADIUS = (int)(PDK_SHADOW_SIGMA * 3.0f) };

static float ClampF(float value, float low, float high)
{
    if (value < low) {
        return low;
    }
    return value > high ? high : value;
}

/* Writes 2*radius+1 normalised weights into `kernel` and returns the radius. */
static int GaussianKernel(float sigma, float *kernel)
{
    const int radius = (int)ceilf(sigma * 3.0f);
    float sum = 0.0f;

    for (int i = -radius; i <= radius; ++i) {
        const float value = expf(-((float)(i * i)) / (2.0f * sigma * sigma));

        kernel[i + radius] = value;
        sum += value;
    }
    for (int i = 0; i <= radius * 2; ++i) {
        kernel[i] /= sum;
    }
    return radius;
}

static void BlurAxis(const float *in, float *out, int size, const float *kernel, int radius,
                     bool horizontal)
{
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float acc = 0.0f;

            for (int k = -radius; k <= radius; ++k) {
                const int sx = horizontal ? x + k : x;
                const int sy = horizontal ? y : y + k;

                if (sx < 0 || sy < 0 || sx >= size || sy >= size) {
                    continue;
                }
                acc += in[sy * size + sx] * kernel[k + radius];
            }
            out[y * size + x] = acc;
        }
    }
}

static float RoundedRectCoverage(float px, float py, float left, float top, float right,
                                 float bottom, float radius)
{
    const float cx = ClampF(px, left + radius, right - radius);
    const float cy = ClampF(py, top + radius, bottom - radius);
    const float dx = px - cx;
    const float dy = py - cy;
    const float distance = sqrtf(dx * dx + dy * dy) - radius;

    return ClampF(0.5f - distance, 0.0f, 1.0f);
}

/* D2D1::BitmapProperties and D2D1::SizeU are C++ helpers in <d2d1.h>, so the structs are
 * filled in directly here. */
static D2D1_BITMAP_PROPERTIES PbgraBitmapProperties(void)
{
    D2D1_BITMAP_PROPERTIES props;

    props.pixelFormat.format = DXGI_FORMAT_B8G8R8A8_UNORM;
    props.pixelFormat.alphaMode = D2D1_ALPHA_MODE_PREMULTIPLIED;
    props.dpiX = 96.0f;
    props.dpiY = 96.0f;
    return props;
}

static PDK_ID2D1Bitmap *CreatePbgraBitmap(PDK_ID2D1RenderTarget *target, int size,
                                          const uint32_t *pixels)
{
    const D2D1_BITMAP_PROPERTIES props = PbgraBitmapProperties();
    D2D1_SIZE_U dimensions;
    PDK_ID2D1Bitmap *bitmap = NULL;

    dimensions.width = (UINT32)size;
    dimensions.height = (UINT32)size;
    if (FAILED(PDK_CALL(target, CreateBitmap, dimensions, pixels, (UINT32)size * 4, &props,
                        &bitmap))) {
        return NULL;
    }
    return bitmap;
}

void ProceduralTextures_Init(ProceduralTextures *textures)
{
    textures->shadow = NULL;
    textures->felt = NULL;
    textures->feltBrush = NULL;
}

PDK_ID2D1Bitmap *ProceduralTextures_Shadow(ProceduralTextures *textures,
                                          PDK_ID2D1RenderTarget *target)
{
    const int size = PDK_SHADOW_SIZE;
    float mask[PDK_SHADOW_SIZE * PDK_SHADOW_SIZE];
    float temp[PDK_SHADOW_SIZE * PDK_SHADOW_SIZE];
    float kernel[PDK_MAX_BLUR_RADIUS * 2 + 1];
    uint32_t pixels[PDK_SHADOW_SIZE * PDK_SHADOW_SIZE];
    int radius;
    const float lo = (float)PDK_SHADOW_PAD;
    const float hi = (float)(PDK_SHADOW_PAD + PDK_SHADOW_CORE);

    if (textures->shadow != NULL || target == NULL) {
        return textures->shadow;
    }

    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            mask[y * size + x] = RoundedRectCoverage((float)x + 0.5f, (float)y + 0.5f, lo, lo,
                                                     hi, hi, 6.0f);
        }
    }
    radius = GaussianKernel(PDK_SHADOW_SIGMA, kernel);
    BlurAxis(mask, temp, size, kernel, radius, true);
    BlurAxis(temp, mask, size, kernel, radius, false);

    for (int i = 0; i < size * size; ++i) {
        const uint32_t alpha = (uint32_t)(ClampF(mask[i], 0.0f, 1.0f) * 255.0f + 0.5f);

        pixels[i] = alpha << 24;
    }
    textures->shadow = CreatePbgraBitmap(target, size, pixels);
    return textures->shadow;
}

PDK_ID2D1BitmapBrush *ProceduralTextures_Felt(ProceduralTextures *textures,
                                             PDK_ID2D1RenderTarget *target)
{
    const int size = PDK_FELT_SIZE;
    uint32_t seed = 0x2468ACEu;
    float noise[PDK_FELT_SIZE * PDK_FELT_SIZE];
    float felt[PDK_FELT_SIZE * PDK_FELT_SIZE];
    uint32_t pixels[PDK_FELT_SIZE * PDK_FELT_SIZE];

    if (textures->feltBrush != NULL || target == NULL) {
        return textures->feltBrush;
    }

    for (int i = 0; i < size * size; ++i) {
        seed = seed * 1664525u + 1013904223u;
        noise[i] = (float)((seed >> 8) & 0xFFFFu) / 65535.0f * 2.0f - 1.0f;
    }
    /* Mix fine grain with short horizontal fibres, wrapping so the tile repeats
     * seamlessly. */
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            float fibre = 0.0f;
            float grain;

            for (int k = -3; k <= 3; ++k) {
                fibre += noise[y * size + ((x + k + size) % size)];
            }
            grain = noise[((y * 7 + 3) % size) * size + ((x * 5 + 11) % size)];
            felt[y * size + x] = fibre / 7.0f * 0.7f + grain * 0.45f;
        }
    }

    for (int i = 0; i < size * size; ++i) {
        const float v = ClampF(felt[i], -1.0f, 1.0f);
        const uint32_t alpha = (uint32_t)(fabsf(v) * 0.55f * 255.0f + 0.5f);
        const uint32_t channel = v > 0.0f ? alpha : 0u;

        pixels[i] = (alpha << 24) | (channel << 16) | (channel << 8) | channel;
    }
    textures->felt = CreatePbgraBitmap(target, size, pixels);
    if (textures->felt != NULL) {
        D2D1_BITMAP_BRUSH_PROPERTIES props;

        props.extendModeX = D2D1_EXTEND_MODE_WRAP;
        props.extendModeY = D2D1_EXTEND_MODE_WRAP;
        props.interpolationMode = D2D1_BITMAP_INTERPOLATION_MODE_LINEAR;
        PDK_CALL(target, CreateBitmapBrush, textures->felt, &props, NULL,
                 &textures->feltBrush);
    }
    return textures->feltBrush;
}

void ProceduralTextures_Reset(ProceduralTextures *textures)
{
    PDK_RELEASE(textures->shadow);
    PDK_RELEASE(textures->feltBrush);
    PDK_RELEASE(textures->felt);
}
