#include "graphics/win_compat.h"

#include "overlays/TalkBubbleOverlay.h"

#include "core/Str.h"
#include "graphics/d2d_c.h"
#include "rules/Scoring.h"
#include "scenes/GameLayout.h"
#include "ui/Widgets.h"

#include <stdlib.h>

enum { PDK_TALK_TEXT_CAP = 160 };

typedef struct TalkBubbleOverlay {
    PlayerId player;
    char text[PDK_TALK_TEXT_CAP];
    float elapsed;
} TalkBubbleOverlay;

enum {
    kBubbleMaxTextWidth = 290,
    kBubblePadX = 16,
    kBubblePadY = 11,
    kBubbleTailHeight = 9
};

static TextStyle BubbleText(void)
{
    TextStyle style = TextStyle_Label(15.5f, 400 /* NORMAL */);

    style.lineHeight = 22.0f;
    return style;
}

static void TalkBubbleOverlay_Update(void *user, float dt)
{
    TalkBubbleOverlay *bubble = (TalkBubbleOverlay *)user;

    bubble->elapsed += dt;
}

static void TalkBubbleOverlay_Render(void *user, RenderContext *context)
{
    const TalkBubbleOverlay *bubble = (const TalkBubbleOverlay *)user;
    const TextStyle style = BubbleText();
    Size textSize;
    float width;
    float height;
    Rect plate;
    Point avatar;
    float top;
    float x;
    Rect box;
    float tailX;
    float pop;
    float alpha;
    D2D1_COLOR_F paper;
    Point tail[3];
    Point scaleCenter;
    Rect boxShadow;
    Rect inner;
    Rect textRect;
    GradientStop stops[2];
    Point from;
    Point to;

    RenderContext_MeasureText(context, bubble->text, &style, (float)kBubbleMaxTextWidth, &textSize);
    width = fminf((float)kBubbleMaxTextWidth, textSize.width) + (float)kBubblePadX * 2.0f;
    height = textSize.height + (float)kBubblePadY * 2.0f;

    plate = GameLayout_PlateFor(bubble->player);
    avatar = GameLayout_AvatarCenter(bubble->player);
    top = plate.y + plate.height + 10.0f + (float)kBubbleTailHeight;
    x = bubble->player == PLAYER_AI1 ? plate.x + 8.0f
                                     : plate.x + plate.width - 8.0f - width;
    box = Rect_Make(x, top, width, height);
    tailX = ClampF(avatar.x, box.x + 22.0f, box.x + box.width - 22.0f);

    pop = EaseOutBackWith(Progress(bubble->elapsed, 0.0f, 0.28f), 1.6f);
    alpha = Clamp01(bubble->elapsed * 8.0f) * Clamp01((3.0f - bubble->elapsed) / 0.4f);
    RenderContext_PushOpacity(context, alpha);
    scaleCenter = Point_Make(tailX, top - (float)kBubbleTailHeight);
    RenderContext_PushScale(context, Lerp(0.82f, 1.0f, pop), scaleCenter);

    boxShadow = Rect_Make(box.x + 4.0f, box.y + 6.0f, box.width - 8.0f, box.height);
    RenderContext_DrawShadow(context, &boxShadow, 9.0f, ColorF_Make(0.0f, 0.0f, 0.0f, 0.45f));

    paper = Rgb(0xF6EEDB, 1.0f);
    from = Point_Make(0.0f, box.y);
    to = Point_Make(0.0f, box.y + box.height);
    stops[0] = GradientStop_Make(0.0f, paper);
    stops[1] = GradientStop_Make(1.0f, Rgb(0xE9DDC2, 1.0f));
    RenderContext_FillRoundedRectBrush(context, &box, 14.0f,
                                       RenderContext_Linear(context, from, to, stops, 2));

    tail[0] = Point_Make(tailX - 9.0f, top + 1.0f);
    tail[1] = Point_Make(tailX, top - (float)kBubbleTailHeight);
    tail[2] = Point_Make(tailX + 9.0f, top + 1.0f);
    RenderContext_FillPolygon(context, tail, 3, paper);

    inner = Rect_Make(box.x + 0.5f, box.y + 0.5f, box.width - 1.0f, box.height - 1.0f);
    RenderContext_StrokeRoundedRect(context, &inner, 14.0f,
                                    WithAlpha(THEME_GOLD_DEEP, 0.55f), 1.0f);

    textRect = Rect_Make(box.x + (float)kBubblePadX, box.y + (float)kBubblePadY,
                         (float)kBubbleMaxTextWidth, textSize.height + 2.0f);
    RenderContext_DrawTextUtf8(context, bubble->text, &textRect, &style, Rgb(0x1F2A24, 1.0f));

    RenderContext_PopTransform(context);
    RenderContext_PopOpacity(context);
}

static bool TalkBubbleOverlay_BlocksInputBelow(void *user)
{
    (void)user;
    return false;
}

static bool TalkBubbleOverlay_Expired(void *user)
{
    const TalkBubbleOverlay *bubble = (const TalkBubbleOverlay *)user;

    return bubble->elapsed > 3.0f;
}

static void TalkBubbleOverlay_Destroy(void *user)
{
    free(user);
}

static const OverlayVtbl kTalkBubbleOverlayVtbl = {
    .Update = TalkBubbleOverlay_Update,
    .Render = TalkBubbleOverlay_Render,
    .BlocksInputBelow = TalkBubbleOverlay_BlocksInputBelow,
    .Expired = TalkBubbleOverlay_Expired,
    .Destroy = TalkBubbleOverlay_Destroy
};

Overlay TalkBubbleOverlay_New(PlayerId player, const char *text)
{
    TalkBubbleOverlay *bubble = (TalkBubbleOverlay *)malloc(sizeof(TalkBubbleOverlay));
    Overlay handle;

    if (bubble == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    bubble->player = player;
    Str_CopyTo(bubble->text, PDK_TALK_TEXT_CAP, text);
    bubble->elapsed = 0.0f;
    handle.vtbl = &kTalkBubbleOverlayVtbl;
    handle.user = bubble;
    return handle;
}
