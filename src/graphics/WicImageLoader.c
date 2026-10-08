#include "graphics/win_compat.h"

#include "graphics/d2d_c.h"

#include "graphics/WicImageLoader.h"

PDK_ID2D1Bitmap *WicImageLoader_LoadBitmapFromMemory(PDK_ID2D1RenderTarget *target,
                                                     IWICImagingFactory *wicFactory,
                                                     const uint8_t *bytes, int count,
                                                     float scale)
{
    IWICStream *stream = NULL;
    IWICBitmapDecoder *decoder = NULL;
    IWICBitmapFrameDecode *frame = NULL;
    IWICBitmapSource *source = NULL;
    IWICBitmapScaler *scaler = NULL;
    IWICFormatConverter *converter = NULL;
    PDK_ID2D1Bitmap *bitmap = NULL;

    if (target == NULL || wicFactory == NULL || bytes == NULL || count <= 0) {
        return NULL;
    }

    if (FAILED(PDK_CALL(wicFactory, CreateStream, &stream))) {
        goto done;
    }
    /*
     * InitializeFromMemory takes a non-const BYTE*, which the C++ version reached with a
     * const_cast.  WIC documents that it does not write to the buffer (the stream is
     * read-only), so the promise is the same; the cast is only needed because the SDK
     * predates const correctness.
     */
    if (FAILED(PDK_CALL(stream, InitializeFromMemory, (BYTE *)(const void *)bytes,
                        (DWORD)count))) {
        goto done;
    }
    if (FAILED(PDK_CALL(wicFactory, CreateDecoderFromStream, (IStream *)stream, NULL,
                        WICDecodeMetadataCacheOnLoad, &decoder))) {
        goto done;
    }
    if (FAILED(PDK_CALL(decoder, GetFrame, 0, &frame))) {
        goto done;
    }

    source = (IWICBitmapSource *)frame;
    PDK_ADDREF(source);
    if (scale < 1.0f) {
        UINT width = 0;
        UINT height = 0;

        PDK_CALL(frame, GetSize, &width, &height);
        if (SUCCEEDED(PDK_CALL(wicFactory, CreateBitmapScaler, &scaler)) &&
            SUCCEEDED(PDK_CALL(scaler, Initialize, (IWICBitmapSource *)frame,
                               (UINT)((float)width * scale + 0.5f),
                               (UINT)((float)height * scale + 0.5f),
                               WICBitmapInterpolationModeFant))) {
            PDK_RELEASE(source);
            source = (IWICBitmapSource *)scaler;
            PDK_ADDREF(source);
        }
    }

    if (FAILED(PDK_CALL(wicFactory, CreateFormatConverter, &converter))) {
        goto done;
    }
    if (FAILED(PDK_CALL(converter, Initialize, source, &GUID_WICPixelFormat32bppPBGRA,
                        WICBitmapDitherTypeNone, NULL, 0.0, WICBitmapPaletteTypeMedianCut))) {
        goto done;
    }
    if (FAILED(PDK_CALL(target, CreateBitmapFromWicBitmap, converter, NULL, &bitmap))) {
        bitmap = NULL;
    }

done:
    PDK_RELEASE(converter);
    PDK_RELEASE(scaler);
    PDK_RELEASE(source);
    PDK_RELEASE(frame);
    PDK_RELEASE(decoder);
    PDK_RELEASE(stream);
    return bitmap;
}
