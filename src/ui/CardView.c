#include "graphics/win_compat.h"

#include "ui/CardView.h"

#include "graphics/d2d_c.h"
#include "resources/CardAtlasData.h"

static const D2D1_COLOR_F kBlack = {0.0f, 0.0f, 0.0f, 1.0f};
static const D2D1_COLOR_F kWhite = {1.0f, 1.0f, 1.0f, 1.0f};

static float MinF(float a, float b)
{
    return a < b ? a : b;
}

static float MaxF(float a, float b)
{
    return a > b ? a : b;
}

/* Draws whatever the caller supplies between the shared treatment. */
typedef void (*CardContentFn)(RenderContext *context, const Rect *rect, void *user);

static void DrawCardCommon(RenderContext *context, const Rect *rect, const CardLook *look,
                           CardContentFn content, void *user)
{
    Rect r = *rect;
    Point center;
    float radius;
    bool rotated;

    r.y -= look->lift;
    center.x = r.x + r.width * 0.5f;
    center.y = r.y + r.height * 0.5f;
    radius = r.width * THEME_CARD_RADIUS_RATIO;
    rotated = look->rotation != 0.0f;
    if (rotated) {
        RenderContext_PushRotation(context, look->rotation, center);
    }
    RenderContext_PushOpacity(context, look->opacity);

    if (look->shadow > 0.0f) {
        const float depth = MaxF(0.0f, look->lift);
        const float blur = MaxF(2.0f, r.width * 0.035f) + depth * 0.14f;
        Rect shadowRect;

        shadowRect.x = r.x + 1.0f;
        shadowRect.y = r.y + r.width * 0.03f + depth * 0.22f;
        shadowRect.width = r.width - 2.0f;
        shadowRect.height = r.height - 1.0f;
        RenderContext_DrawShadow(context, &shadowRect, blur,
                                 WithAlpha(kBlack,
                                           (0.42f - MinF(0.14f, depth * 0.004f)) * look->shadow));
    }
    if (look->glow > 0.0f) {
        Rect glowRect;

        glowRect.x = r.x - 1.0f;
        glowRect.y = r.y - 1.0f;
        glowRect.width = r.width + 2.0f;
        glowRect.height = r.height + 2.0f;
        RenderContext_DrawShadow(context, &glowRect, 7.0f,
                                 WithAlpha(look->glowColor, 0.85f * look->glow));
    }

    content(context, &r, user);

    if (look->hover > 0.0f) {
        RenderContext_FillRoundedRect(context, &r, radius, WithAlpha(kWhite, 0.10f * look->hover));
    }
    if (look->selected) {
        Rect inset;

        inset.x = r.x + 0.75f;
        inset.y = r.y + 0.75f;
        inset.width = r.width - 1.5f;
        inset.height = r.height - 1.5f;
        RenderContext_StrokeRoundedRect(context, &inset, radius, WithAlpha(THEME_GOLD, 0.95f),
                                        1.6f);
    }
    if (look->glow > 0.0f) {
        Rect inset;

        inset.x = r.x + 0.75f;
        inset.y = r.y + 0.75f;
        inset.width = r.width - 1.5f;
        inset.height = r.height - 1.5f;
        RenderContext_StrokeRoundedRect(context, &inset, radius,
                                        WithAlpha(look->glowColor, look->glow), 2.0f);
    }

    RenderContext_PopOpacity(context);
    if (rotated) {
        RenderContext_PopTransform(context);
    }
}

static void DrawAtlasRect(RenderContext *context, SpriteAtlas *atlas, D2D1_RECT_U source,
                          const Rect *dest)
{
    const CardAtlasInfo *info = GetCardAtlasInfo();
    const float requested =
        dest->width * RenderContext_View(context).scale / (float)info->cardWidth;
    float levelScale = 1.0f;
    PDK_ID2D1Bitmap *bitmap = SpriteAtlas_BitmapFor(atlas, requested, &levelScale);
    D2D1_RECT_F src;

    /* Half-texel inset keeps bilinear sampling from bleeding into the neighbouring card. */
    src.left = (float)source.left * levelScale + 0.5f;
    src.top = (float)source.top * levelScale + 0.5f;
    src.right = (float)source.right * levelScale - 0.5f;
    src.bottom = (float)source.bottom * levelScale - 0.5f;
    RenderContext_DrawBitmapRect(context, bitmap, dest, &src, 1.0f);
}

typedef struct FaceContent {
    Card card;
    SpriteAtlas *atlas;
} FaceContent;

typedef struct BackContent {
    SpriteAtlas *atlas;
} BackContent;

static void DrawFaceContent(RenderContext *context, const Rect *r, void *user)
{
    FaceContent *content = (FaceContent *)user;
    float radius;

    if (SpriteAtlas_Loaded(content->atlas)) {
        DrawAtlasRect(context, content->atlas, CardSourceRect(content->card), r);
        return;
    }
    radius = r->width * THEME_CARD_RADIUS_RATIO;
    RenderContext_FillRoundedRect(context, r, radius, THEME_IVORY);
    RenderContext_StrokeRoundedRect(context, r, radius, WithAlpha(kBlack, 0.25f), 1.0f);
    {
        char text[8];
        TextStyle style = TextStyle_Default();

        Card_ToString(content->card, text, (int)sizeof(text));
        style.size = r->width * 0.2f;
        style.weight = DWRITE_FONT_WEIGHT_SEMI_BOLD;
        style.align = DWRITE_TEXT_ALIGNMENT_CENTER;
        style.valign = DWRITE_PARAGRAPH_ALIGNMENT_CENTER;
        RenderContext_DrawTextUtf8(context, text, r, &style, THEME_INK);
    }
}

static void DrawBackContent(RenderContext *context, const Rect *r, void *user)
{
    BackContent *content = (BackContent *)user;
    float radius;

    if (SpriteAtlas_Loaded(content->atlas)) {
        DrawAtlasRect(context, content->atlas, CardBackSourceRect(), r);
        return;
    }
    radius = r->width * THEME_CARD_RADIUS_RATIO;
    RenderContext_FillRoundedRect(context, r, radius, Rgb(0x1D4E6B, 1.0f));
    RenderContext_StrokeRoundedRect(context, r, radius, WithAlpha(THEME_IVORY, 0.8f), 1.0f);
}

void CardView_DrawFace(RenderContext *context, SpriteAtlas *atlas, Card card, const Rect *rect,
                       const CardLook *look)
{
    FaceContent content;

    content.card = card;
    content.atlas = atlas;
    DrawCardCommon(context, rect, look, DrawFaceContent, &content);
}

void CardView_DrawBack(RenderContext *context, SpriteAtlas *atlas, const Rect *rect,
                       const CardLook *look)
{
    BackContent content;

    content.atlas = atlas;
    DrawCardCommon(context, rect, look, DrawBackContent, &content);
}
