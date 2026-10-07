#include "graphics/win_compat.h"

#include "scenes/StatsScene.h"

#include "app/App.h"
#include "graphics/d2d_c.h"
#include "scenes/SceneCommon.h"
#include "stats/StatStore.h"
#include "ui/Anim.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <stdlib.h>
#include <string.h>

enum { kButtonCapacity = 1 };
enum { kPeriodCount = 3 };
/* "最高单局" can reach 64 and the three-seat total is bounded by 3 * 16 * 32, so this is generous. */
enum { kNumberCapacity = 32 };

typedef struct StatsScene {
    void *app;
    Button buttons[kButtonCapacity];
    int buttonCount;
    StatSummary today;
    StatSummary month;
    StatSummary history;
    float elapsed;
} StatsScene;

static const float kCardTop = 128.0f;
static const float kCardWidth = 372.0f;
static const float kCardHeight = 462.0f;
static const float kCardGap = 22.0f;

/* The old `Number`/`Signed` built a std::string; C has no stream formatting, so these write the
 * same bytes into a caller-owned buffer. */
static void StatsScene_AppendNumber(char *out, int cap, int value)
{
    char digits[16];
    int count = 0;
    int length = 0;
    unsigned int magnitude = value < 0 ? (unsigned int)0 - (unsigned int)value : (unsigned int)value;

    do {
        digits[count++] = (char)('0' + (int)(magnitude % 10u));
        magnitude /= 10u;
    } while (magnitude != 0 && count < (int)sizeof(digits));
    if (value < 0 && length + 1 < cap) {
        out[length++] = '-';
    }
    while (count > 0 && length + 1 < cap) {
        out[length++] = digits[--count];
    }
    out[length] = '\0';
}

static void StatsScene_AppendText(char *out, int cap, const char *text)
{
    int length = (int)strlen(out);

    Str_CopyTo(out + length, cap - length, text);
}

static void StatsScene_Signed(char *out, int cap, int value)
{
    if (value > 0) {
        out[0] = '+';
        StatsScene_AppendNumber(out + 1, cap > 0 ? cap - 1 : 0, value);
        return;
    }
    StatsScene_AppendNumber(out, cap, value);
}

static int StatsScene_Abs(int value)
{
    return value < 0 ? -value : value;
}

static int StatsScene_Max(int a, int b)
{
    return a > b ? a : b;
}

/*
 * The C++ version drew the three period cards through a capturing lambda.  C has no closures, so
 * the captured state travels in an explicit struct and the body is a helper.
 */
typedef struct StatsBlock {
    RenderContext *context;
    const char *const *names;
    float elapsed;
} StatsBlock;

static void StatsScene_DrawBlock(const StatsBlock *block, int index, const char *title,
                                 const StatSummary *summary)
{
    RenderContext *context = block->context;
    const float appear =
        EaseOutCubic(Progress(block->elapsed, 0.05f + (float)index * 0.08f, 0.5f));
    const float grow = EaseOutCubic(Progress(block->elapsed, 0.3f + (float)index * 0.08f, 0.8f));
    const float totalWidth = kCardWidth * 3.0f + kCardGap * 2.0f;
    Rect card = Rect_Make(640.0f - totalWidth * 0.5f + (float)index * (kCardWidth + kCardGap),
                          kCardTop, kCardWidth, kCardHeight);
    float left = card.x + 30.0f;
    float right = card.x + card.width - 30.0f;
    TextStyle titleStyle = TextStyle_Kai(27.0f);
    TextStyle scoreStyle = TextStyle_Label(60.0f, 600);
    TextStyle faintStyle = TextStyle_Label(13.0f, 400);
    TextStyle valueStyle = TextStyle_Label(14.5f, 600);
    TextStyle nameStyle = TextStyle_Label(14.5f, 400);
    TextStyle miniValueStyle = TextStyle_Centered(TextStyle_Label(26.0f, 600));
    TextStyle miniLabelStyle = TextStyle_Centered(TextStyle_Label(13.0f, 400));
    Rect titleRect = Rect_Make(left, card.y + 24.0f, 200.0f, 36.0f);
    ChipStyle rounds = ChipStyle_Default();
    char roundsText[kNumberCapacity];
    char scoreText[kNumberCapacity];
    int myScore;
    Rect scoreRect;
    Rect myScoreLabelRect;
    Rect compareTitleRect;
    int maxAbs = 1;
    float barLeft;
    float barRight;
    float zero;
    float half;
    static const char *const kMiniLabels[3] = {"炸弹", "关圆鸡", "最高单局"};
    const int miniValues[3] = {summary->bombs, summary->springLosers,
                               summary->bestSingleRoundPlayerScore};
    float cellWidth = (right - left) / 3.0f;

    RenderContext_PushOpacity(context, appear);
    RenderContext_PushTranslation(context, 0.0f, (1.0f - appear) * 18.0f);
    Widgets_DrawPanel(context, &card, NULL);

    titleStyle.weight = 700; /* BOLD, matching ui::Kai */
    RenderContext_DrawTextUtf8(context, title, &titleRect, &titleStyle, THEME_GOLD_LIGHT);

    rounds.fontSize = 13.5f;
    rounds.height = 26.0f;
    StatsScene_AppendNumber(roundsText, kNumberCapacity, summary->rounds);
    StatsScene_AppendText(roundsText, kNumberCapacity, " 局");
    Widgets_DrawChip(context, Point_Make(right, card.y + 43.0f), UI_ANCHOR_RIGHT, roundsText,
                     &rounds);

    myScore = RoundToInt((float)summary->scores[0] * grow);
    StatsScene_Signed(scoreText, kNumberCapacity, myScore);
    scoreRect = Rect_Make(left, card.y + 76.0f, 300.0f, 76.0f);
    RenderContext_DrawTextUtf8(context, scoreText, &scoreRect, &scoreStyle,
                               ScoreColor(summary->scores[0]));
    myScoreLabelRect = Rect_Make(left + 2.0f, card.y + 152.0f, 200.0f, 20.0f);
    RenderContext_DrawTextUtf8(context, "我的得分", &myScoreLabelRect, &faintStyle, THEME_FAINT);
    Widgets_DrawHairline(context, card.x + 16.0f, card.x + card.width - 16.0f, card.y + 190.0f,
                         0.35f);

    compareTitleRect = Rect_Make(left, card.y + 204.0f, 200.0f, 20.0f);
    RenderContext_DrawTextUtf8(context, "三家对比", &compareTitleRect, &faintStyle, THEME_FAINT);

    for (int i = 0; i < 3; ++i) {
        maxAbs = StatsScene_Max(maxAbs, StatsScene_Abs(summary->scores[i]));
    }
    barLeft = left + 74.0f;
    barRight = right - 58.0f;
    zero = (barLeft + barRight) * 0.5f;
    half = (barRight - barLeft) * 0.5f;

    nameStyle.valign = 2; /* CENTER */
    nameStyle.wrap = false;
    nameStyle.ellipsis = true;
    valueStyle.align = 2;  /* TRAILING */
    valueStyle.valign = 2; /* CENTER */
    for (int i = 0; i < 3; ++i) {
        const float rowY = card.y + 236.0f + (float)i * 38.0f;
        Rect nameRect = Rect_Make(left, rowY, 68.0f, 26.0f);
        Rect track = Rect_Make(barLeft, rowY + 9.0f, barRight - barLeft, 8.0f);
        const int score = summary->scores[i];
        const float length = half * (float)StatsScene_Abs(score) / (float)maxAbs * grow;
        char recordText[kNumberCapacity];
        Rect valueRect = Rect_Make(right - 56.0f, rowY, 56.0f, 26.0f);

        RenderContext_DrawTextUtf8(context, block->names[i], &nameRect, &nameStyle,
                                   i == 0 ? THEME_IVORY : THEME_MUTED);
        RenderContext_FillRoundedRect(context, &track, 4.0f, WithAlpha(THEME_INK, 0.9f));
        RenderContext_DrawLine(context, Point_Make(zero, rowY + 5.0f),
                               Point_Make(zero, rowY + 21.0f), WithAlpha(THEME_GOLD, 0.35f), 1.0f);
        if (length > 0.5f) {
            const D2D1_COLOR_F color = ScoreColor(score);
            Rect bar = score >= 0 ? Rect_Make(zero, rowY + 9.0f, length, 8.0f)
                                  : Rect_Make(zero - length, rowY + 9.0f, length, 8.0f);
            GradientStop stops[2];

            stops[0] = GradientStop_Make(0.0f, score >= 0 ? WithAlpha(color, 0.45f) : color);
            stops[1] = GradientStop_Make(1.0f, score >= 0 ? color : WithAlpha(color, 0.45f));
            RenderContext_FillRoundedRectBrush(
                context, &bar, 4.0f,
                RenderContext_Linear(context, Point_Make(bar.x, bar.y),
                                     Point_Make(bar.x + bar.width, bar.y), stops, 2));
        }
        StatsScene_Signed(recordText, kNumberCapacity, score);
        RenderContext_DrawTextUtf8(context, recordText, &valueRect, &valueStyle, ScoreColor(score));
    }
    Widgets_DrawHairline(context, card.x + 16.0f, card.x + card.width - 16.0f, card.y + 358.0f,
                         0.35f);

    for (int i = 0; i < 3; ++i) {
        const float cx = left + cellWidth * (float)i;
        char valueText[kNumberCapacity];
        Rect valueRect = Rect_Make(cx, card.y + 376.0f, cellWidth, 34.0f);
        Rect labelRect = Rect_Make(cx, card.y + 412.0f, cellWidth, 20.0f);

        StatsScene_AppendNumber(valueText, kNumberCapacity, miniValues[i]);
        RenderContext_DrawTextUtf8(context, valueText, &valueRect, &miniValueStyle, THEME_IVORY);
        RenderContext_DrawTextUtf8(context, kMiniLabels[i], &labelRect, &miniLabelStyle,
                                   THEME_FAINT);
        if (i > 0) {
            RenderContext_DrawLine(context, Point_Make(cx, card.y + 382.0f),
                                   Point_Make(cx, card.y + 428.0f), WithAlpha(THEME_GOLD, 0.15f),
                                   1.0f);
        }
    }
    RenderContext_PopTransform(context);
    RenderContext_PopOpacity(context);
}

static void StatsScene_OnEnter(void *user)
{
    StatsScene *scene = (StatsScene *)user;
    StatStore store;
    char today[PDK_DATE_KEY_CAP];
    char month[PDK_DATE_KEY_CAP];
    int length;

    StatStore_Init(&store, NULL);
    TodayDateKey(today, PDK_DATE_KEY_CAP);
    StatStore_SummarizeDay(&store, today, &scene->today);

    /* The old code took today.substr(0, 6). */
    Str_CopyTo(month, PDK_DATE_KEY_CAP, today);
    length = (int)strlen(month);
    if (length > 6) {
        month[6] = '\0';
    }
    StatStore_SummarizeMonth(&store, month, &scene->month);
    StatStore_SummarizeHistory(&store, &scene->history);
}

static void StatsScene_Update(void *user, float dt)
{
    StatsScene *scene = (StatsScene *)user;

    scene->elapsed += dt;
    ButtonGroup_UpdateAll(scene->buttons, scene->buttonCount, dt);
}

static void StatsScene_Render(void *user, RenderContext *context)
{
    const StatsScene *scene = (const StatsScene *)user;
    AppSettings settings;
    char playerName[PDK_PLAYER_NAME_CAP];
    const char *names[kPeriodCount];
    StatsBlock block;
    Rect footerRect = Rect_Make(0.0f, 618.0f, 1280.0f, 22.0f);
    TextStyle footerStyle = TextStyle_Centered(TextStyle_Label(13.0f, 400));

    App_GetSettings(scene->app, &settings);
    Str_CopyTo(playerName, PDK_PLAYER_NAME_CAP,
               settings.playerName[0] == '\0' ? "玩家" : settings.playerName);
    names[0] = playerName;
    names[1] = "AI1";
    names[2] = "AI2";

    RenderContext_Clear(context, THEME_ROOM);
    Widgets_DrawRoomBackground(context, Point_Make(640.0f, 300.0f));
    SceneCommon_DrawPageHeader(context, "战绩", "每一局都单独记录，今日、本月与历史从记录实时汇总");

    block.context = context;
    block.names = names;
    block.elapsed = scene->elapsed;
    StatsScene_DrawBlock(&block, 0, "今日", &scene->today);
    StatsScene_DrawBlock(&block, 1, "本月", &scene->month);
    StatsScene_DrawBlock(&block, 2, "历史", &scene->history);

    RenderContext_DrawTextUtf8(context, "记录保存在程序运行目录的 stat 文件夹", &footerRect,
                               &footerStyle, WithAlpha(THEME_FAINT, 0.85f));
    ButtonGroup_DrawAll(context, scene->buttons, scene->buttonCount);
}

static bool StatsScene_OnMouseMove(void *user, float x, float y)
{
    StatsScene *scene = (StatsScene *)user;

    ButtonGroup_UpdateHover(scene->buttons, scene->buttonCount, x, y);
    return true;
}

static bool StatsScene_OnMouseDown(void *user, float x, float y)
{
    StatsScene *scene = (StatsScene *)user;

    if (ButtonGroup_Hit(scene->buttons, scene->buttonCount, x, y) >= 0) {
        App_PlaySound(scene->app, SOUND_CANCEL);
        App_ShowStart(scene->app);
        return true;
    }
    return false;
}

static void StatsScene_Destroy(void *user)
{
    free(user);
}

static const SceneVtbl kStatsSceneVtbl = {
    .OnEnter = StatsScene_OnEnter,
    .Update = StatsScene_Update,
    .Render = StatsScene_Render,
    .OnMouseMove = StatsScene_OnMouseMove,
    .OnMouseDown = StatsScene_OnMouseDown,
    .Destroy = StatsScene_Destroy
};

Scene StatsScene_New(void *app)
{
    StatsScene *scene = (StatsScene *)malloc(sizeof(StatsScene));
    Scene handle;

    if (scene == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    scene->app = app;
    scene->buttons[0] = SceneCommon_BackButton();
    scene->buttonCount = kButtonCapacity;
    StatSummary_Init(&scene->today);
    StatSummary_Init(&scene->month);
    StatSummary_Init(&scene->history);
    scene->elapsed = 0.0f;
    handle.vtbl = &kStatsSceneVtbl;
    handle.user = scene;
    return handle;
}
