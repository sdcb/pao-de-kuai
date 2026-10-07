#include "graphics/win_compat.h"

#include "overlays/RoundResultOverlay.h"

#include "app/AppApi.h"
#include "core/Str.h"
#include "graphics/d2d_c.h"
#include "rules/Scoring.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <stdlib.h>

enum { kButtonCapacity = 2 };
enum { kBadgeCapacity = 3 };

typedef struct RoundResultOverlay {
    void *app;
    RoundRecord record;
    Button buttons[kButtonCapacity];
    int buttonCount;
    float elapsed;
} RoundResultOverlay;

static const Rect kPanel = {330.0f, 128.0f, 620.0f, 470.0f};
static const float kTableLeft = 384.0f;
static const float kTableRight = 896.0f;
static const float kHeaderY = 322.0f;
static const float kRowTop = 346.0f;
static const float kRowHeight = 46.0f;

/* The old `Signed` built a std::string with core::AppendNumber; C has no stream formatting, so
 * this writes the same bytes into a caller-owned buffer. */
static void RoundResultOverlay_Signed(char *out, int cap, int value)
{
    char digits[16];
    int count = 0;
    int length = 0;
    unsigned int magnitude;

    if (value > 0) {
        out[length++] = '+';
    }
    magnitude = value < 0 ? (unsigned int)0 - (unsigned int)value : (unsigned int)value;
    do {
        digits[count++] = (char)('0' + (int)(magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0 && count < (int)sizeof(digits));
    if (value < 0) {
        out[length++] = '-';
    }
    while (count > 0 && length + 1 < cap) {
        out[length++] = digits[--count];
    }
    out[length] = '\0';
}

/* One badge line, e.g. "炸弹 x 2  (不参与翻倍)".  `count` is appended in decimal. */
static void RoundResultOverlay_Badge(Str *out, const char *label, int count, const char *note)
{
    Str_Append(out, label);
    Str_AppendNumber(out, count);
    Str_Append(out, note);
}

static void RoundResultOverlay_Update(void *user, float dt)
{
    RoundResultOverlay *overlay = (RoundResultOverlay *)user;

    overlay->elapsed += dt;
    ButtonGroup_UpdateAll(overlay->buttons, overlay->buttonCount, dt);
}

static void RoundResultOverlay_Render(void *user, RenderContext *context)
{
    const RoundResultOverlay *overlay = (const RoundResultOverlay *)user;
    const bool win = overlay->record.winner == PLAYER_HUMAN;
    TextStyle title;
    TextStyle scoreStyle;
    TextStyle header;
    TextStyle headerCenter;
    TextStyle headerRight;
    TextStyle centered;
    TextStyle scoreRight;
    TextStyle nameStyle;
    float stamp;
    char scoreText[16];
    Str scoreLine;
    Str badges[kBadgeCapacity];
    int badgeCount = 0;
    int scoredBombs = 0;
    int beatenBombs = 0;
    int i;
    const char *names[3];
    Size nameSize;
    float nameWidth;
    float countUp;
    Rect rect;
    Point glowCenter;
    Point sealCenter;
    Point anchor;
    GradientStop stops[2];

    glowCenter = Point_Make(640.0f, 150.0f);
    sealCenter = Point_Make(640.0f, 150.0f);

    Widgets_BeginModal(context, &kPanel, overlay->elapsed);
    Widgets_DrawPanel(context, &kPanel, NULL);
    if (win) {
        Widgets_DrawRadialGlow(context, glowCenter, 190.0f, WithAlpha(THEME_GOLD, 0.16f), 0.0f);
    }

    stamp = EaseOutBackWith(Progress(overlay->elapsed, 0.12f, 0.35f), 1.8f);
    RenderContext_PushOpacity(context, Clamp01(Progress(overlay->elapsed, 0.12f, 0.12f)));
    RenderContext_PushScale(context, Lerp(1.9f, 1.0f, stamp), sealCenter);
    Widgets_DrawSeal(context, sealCenter, 88.0f, win ? "胜" : "负",
                     win ? THEME_CINNABAR : Rgb(0x4A5A54, 1.0f), win ? -6.0f : 5.0f, 58.0f);
    RenderContext_PopTransform(context);
    RenderContext_PopOpacity(context);

    title = TextStyle_Centered(TextStyle_Kai(32.0f));
    rect = Rect_Make(kPanel.x, 206.0f, kPanel.width, 42.0f);
    RenderContext_DrawTextUtf8(context, win ? "本局大胜" : "惜败一局", &rect, &title,
                               win ? THEME_GOLD_LIGHT : THEME_IVORY);
    scoreStyle = TextStyle_Centered(TextStyle_Label(17.0f, 600 /* SEMI_BOLD */));
    rect = Rect_Make(kPanel.x, 248.0f, kPanel.width, 26.0f);
    RoundResultOverlay_Signed(scoreText, (int)sizeof(scoreText), overlay->record.scores[0]);
    // The original drew "本局得分  " + Signed(score) as one centred string.  C has no operator+
    // for that, and the port kept only the number, silently losing the label -- the baseline
    // comparison in tools/compare_screenshots.py is what caught it.  Built with Str, which is what
    // the C++ core::AppendNumber call compiled to, rather than by hand-splicing a byte offset into
    // a buffer whose label is not ASCII.
    Str_Init(&scoreLine);
    Str_Append(&scoreLine, "本局得分  ");
    Str_Append(&scoreLine, scoreText);
    RenderContext_DrawTextUtf8(context, Str_CStr(&scoreLine), &rect, &scoreStyle,
                               ScoreColor(overlay->record.scores[0]));
    Str_Free(&scoreLine);

    /* Special-event badges. */
    for (i = 0; i < overlay->record.bombCount; ++i) {
        if (overlay->record.bombs[i].beaten) {
            ++beatenBombs;
        } else {
            ++scoredBombs;
        }
    }
    if (scoredBombs > 0) {
        Str_Init(&badges[badgeCount]);
        RoundResultOverlay_Badge(&badges[badgeCount], "炸弹 × ", scoredBombs, "  (不参与翻倍)");
        ++badgeCount;
    }
    if (beatenBombs > 0) {
        Str_Init(&badges[badgeCount]);
        RoundResultOverlay_Badge(&badges[badgeCount], "炸弹被压 × ", beatenBombs, "  (不计分)");
        ++badgeCount;
    }
    if (overlay->record.spring.enabled) {
        Str_Init(&badges[badgeCount]);
        RoundResultOverlay_Badge(&badges[badgeCount], "关圆鸡 × ", overlay->record.spring.loserCount,
                                 "");
        ++badgeCount;
    }
    if (badgeCount > 0) {
        ChipStyle chip = ChipStyle_Default();
        Rect chipRects[kBadgeCapacity];
        const Point chipAnchor = Point_Make(0.0f, 292.0f);
        float total = -10.0f;
        float x;

        chip.fontSize = 13.5f;
        chip.height = 26.0f;
        chip.fill = WithAlpha(THEME_CINNABAR_DEEP, 0.55f);
        chip.stroke = WithAlpha(THEME_CINNABAR_LIGHT, 0.7f);
        chip.text = THEME_IVORY;

        for (i = 0; i < badgeCount; ++i) {
            chipRects[i] =
                Widgets_ChipRect(context, chipAnchor, UI_ANCHOR_LEFT, Str_CStr(&badges[i]), &chip);
            total += chipRects[i].width + 10.0f;
        }
        x = 640.0f - total * 0.5f;
        for (i = 0; i < badgeCount; ++i) {
            chipRects[i].x = x;
            Widgets_DrawChipInRect(context, &chipRects[i], Str_CStr(&badges[i]), &chip);
            x += chipRects[i].width + 10.0f;
        }
    }
    for (i = 0; i < badgeCount; ++i) {
        Str_Free(&badges[i]);
    }

    header = TextStyle_Label(13.0f, 400 /* NORMAL */);
    header.wrap = false;
    rect = Rect_Make(kTableLeft + 48.0f, kHeaderY - 10.0f, 160.0f, 20.0f);
    RenderContext_DrawTextUtf8(context, "玩家", &rect, &header, THEME_FAINT);
    headerCenter = header;
    headerCenter.align = 2; /* DWRITE_TEXT_ALIGNMENT_CENTER */
    rect = Rect_Make(640.0f, kHeaderY - 10.0f, 100.0f, 20.0f);
    RenderContext_DrawTextUtf8(context, "剩余牌", &rect, &headerCenter, THEME_FAINT);
    headerRight = header;
    headerRight.align = 2; /* DWRITE_TEXT_ALIGNMENT_TRAILING */
    rect = Rect_Make(kTableRight - 130.0f, kHeaderY - 10.0f, 120.0f, 20.0f);
    RenderContext_DrawTextUtf8(context, "本局得分", &rect, &headerRight, THEME_FAINT);

    names[0] = overlay->record.playerName[0] == '\0' ? "玩家" : overlay->record.playerName;
    names[1] = "AI1";
    names[2] = "AI2";
    centered = TextStyle_Centered(TextStyle_Label(17.0f, 400));
    scoreRight = TextStyle_Label(21.0f, 600 /* SEMI_BOLD */);
    scoreRight.align = 2; /* DWRITE_TEXT_ALIGNMENT_TRAILING */
    scoreRight.valign = 2; /* DWRITE_PARAGRAPH_ALIGNMENT_CENTER */
    countUp = EaseOutCubic(Progress(overlay->elapsed, 0.35f, 0.9f));
    for (i = 0; i < 3; ++i) {
        const float rowAppear =
            EaseOutCubic(Progress(overlay->elapsed, 0.2f + (float)i * 0.08f, 0.35f));
        const float y = kRowTop + (float)i * kRowHeight;
        const bool winner = PlayerIndex(overlay->record.winner) == i;
        char avatar[8];
        char remainingText[16];
        char shownText[16];
        Rect avatarRect;
        int shown;

        RenderContext_PushOpacity(context, rowAppear);
        RenderContext_PushTranslation(context, 0.0f, (1.0f - rowAppear) * 8.0f);
        if (winner) {
            const Point from = Point_Make(kTableLeft, y);
            const Point to = Point_Make(kTableRight, y);
            const Rect row = Rect_Make(kTableLeft - 8.0f, y + 3.0f,
                                       kTableRight - kTableLeft + 16.0f, kRowHeight - 6.0f);

            stops[0] = GradientStop_Make(0.0f, WithAlpha(THEME_GOLD, 0.20f));
            stops[1] = GradientStop_Make(1.0f, WithAlpha(THEME_GOLD, 0.02f));
            RenderContext_FillRoundedRectBrush(context, &row, 12.0f,
                                               RenderContext_Linear(context, from, to, stops, 2));
        }
        avatarRect = Rect_Make(kTableLeft, y + 8.0f, 30.0f, 30.0f);
        Widgets_AvatarLabel(names[i], avatar, (int)sizeof(avatar));
        Widgets_DrawAvatar(context, &avatarRect, avatar, false, 0.0f);

        nameStyle = TextStyle_Label(17.0f, winner ? 600 /* SEMI_BOLD */ : 400 /* NORMAL */);
        nameStyle.valign = 2; /* DWRITE_PARAGRAPH_ALIGNMENT_CENTER */
        nameStyle.wrap = false;
        nameStyle.ellipsis = true;
        rect = Rect_Make(kTableLeft + 48.0f, y, 170.0f, kRowHeight);
        RenderContext_DrawTextUtf8(context, names[i], &rect, &nameStyle,
                                   winner ? THEME_GOLD_LIGHT : THEME_IVORY);
        if (winner) {
            ChipStyle chip = ChipStyle_Default();

            chip.fontSize = 12.0f;
            chip.height = 20.0f;
            chip.padX = 8.0f;
            chip.fill = WithAlpha(THEME_GOLD, 0.9f);
            chip.stroke = ColorF_Make(0.0f, 0.0f, 0.0f, 0.0f);
            chip.text = THEME_GOLD_INK;
            chip.weight = 600; /* DWRITE_FONT_WEIGHT_SEMI_BOLD */
            RenderContext_MeasureText(context, names[i], &nameStyle, 170.0f, &nameSize);
            nameWidth = nameSize.width < 170.0f ? nameSize.width : 170.0f;
            anchor = Point_Make(kTableLeft + 58.0f + nameWidth, y + kRowHeight * 0.5f);
            Widgets_DrawChip(context, anchor, UI_ANCHOR_LEFT, "赢家", &chip);
        }

        RoundResultOverlay_Signed(remainingText, (int)sizeof(remainingText),
                                  overlay->record.remainingCards[i]);
        rect = Rect_Make(640.0f, y, 100.0f, kRowHeight);
        RenderContext_DrawTextUtf8(context, remainingText, &rect, &centered, THEME_MUTED);

        shown = RoundToInt((float)overlay->record.scores[i] * countUp);
        RoundResultOverlay_Signed(shownText, (int)sizeof(shownText), shown);
        rect = Rect_Make(kTableRight - 130.0f, y, 120.0f, kRowHeight);
        RenderContext_DrawTextUtf8(context, shownText, &rect, &scoreRight,
                                   ScoreColor(overlay->record.scores[i]));
        if (i < 2) {
            Widgets_DrawHairline(context, kTableLeft, kTableRight, y + kRowHeight, 0.22f);
        }
        RenderContext_PopTransform(context);
        RenderContext_PopOpacity(context);
    }

    ButtonGroup_DrawAll(context, overlay->buttons, overlay->buttonCount);
    Widgets_EndModal(context);
}

static bool RoundResultOverlay_BlocksInputBelow(void *user)
{
    (void)user;
    return true;
}

static bool RoundResultOverlay_OnMouseMove(void *user, float x, float y)
{
    RoundResultOverlay *overlay = (RoundResultOverlay *)user;

    ButtonGroup_UpdateHover(overlay->buttons, overlay->buttonCount, x, y);
    return true;
}

static bool RoundResultOverlay_OnMouseDown(void *user, float x, float y)
{
    RoundResultOverlay *overlay = (RoundResultOverlay *)user;
    const int hit = ButtonGroup_Hit(overlay->buttons, overlay->buttonCount, x, y);

    if (hit == 0) {
        App_PlaySound(overlay->app, SOUND_CONFIRM);
        App_RestartCurrentGame(overlay->app);
    } else if (hit == 1) {
        App_PlaySound(overlay->app, SOUND_CANCEL);
        App_ShowStart(overlay->app);
    }
    return true;
}

static void RoundResultOverlay_Destroy(void *user)
{
    free(user);
}

static const OverlayVtbl kRoundResultOverlayVtbl = {
    .Update = RoundResultOverlay_Update,
    .Render = RoundResultOverlay_Render,
    .BlocksInputBelow = RoundResultOverlay_BlocksInputBelow,
    .OnMouseMove = RoundResultOverlay_OnMouseMove,
    .OnMouseDown = RoundResultOverlay_OnMouseDown,
    .Destroy = RoundResultOverlay_Destroy
};

Overlay RoundResultOverlay_New(void *app, const RoundRecord *record)
{
    RoundResultOverlay *overlay = (RoundResultOverlay *)malloc(sizeof(RoundResultOverlay));
    Overlay handle;

    if (overlay == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    overlay->app = app;
    overlay->record = *record;
    overlay->buttons[0] = Button_Make(Rect_Make(466.0f, 524.0f, 168.0f, 48.0f), "再来一局",
                                      UI_BUTTON_PRIMARY);
    overlay->buttons[1] = Button_Make(Rect_Make(646.0f, 524.0f, 168.0f, 48.0f), "主菜单",
                                      UI_BUTTON_SECONDARY);
    overlay->buttonCount = kButtonCapacity;
    overlay->elapsed = 0.0f;
    handle.vtbl = &kRoundResultOverlayVtbl;
    handle.user = overlay;
    return handle;
}
