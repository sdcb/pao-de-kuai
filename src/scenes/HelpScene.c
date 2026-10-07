#include "graphics/win_compat.h"

#include "scenes/HelpScene.h"

#include "app/AppApi.h"
#include "graphics/d2d_c.h"
#include "rules/Card.h"
#include "rules/RuleText.h"
#include "scenes/SceneCommon.h"
#include "ui/Anim.h"
#include "ui/CardView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <stdlib.h>

enum { kButtonCapacity = 1 };
/* The tallest sample is the plane (8 cards). */
enum { kSampleCardsMax = 8 };

typedef struct HelpScene {
    void *app;
    Button buttons[kButtonCapacity];
    int buttonCount;
    float elapsed;
} HelpScene;

static const Rect kRulesPanel = {56.0f, 120.0f, 650.0f, 572.0f};
static const Rect kSidePanel = {724.0f, 120.0f, 500.0f, 572.0f};

/*
 * The C++ version built one std::vector<Card> per sample out of braced initialiser lists.  C
 * arrays cannot be assigned and a function cannot return a variable-length aggregate, so the
 * caller owns the buffer and gets the count back.  Both take an out parameter rather than
 * returning `Cards`, which is 388 bytes by value.
 */
static void HelpScene_PushCards(Cards *out, const Card *cards, int count)
{
    Cards_Clear(out);
    for (int i = 0; i < count; ++i) {
        Cards_Push(out, cards[i]);
    }
}

static void HelpScene_CardsOf(Cards *out, int index)
{
    static const Card pair[2] = {{RANK_NINE, SUIT_SPADES}, {RANK_NINE, SUIT_HEARTS}};
    static const Card straight[5] = {{RANK_THREE, SUIT_CLUBS},
                                     {RANK_FOUR, SUIT_HEARTS},
                                     {RANK_FIVE, SUIT_SPADES},
                                     {RANK_SIX, SUIT_DIAMONDS},
                                     {RANK_SEVEN, SUIT_CLUBS}};
    static const Card doublePair[4] = {{RANK_THREE, SUIT_SPADES},
                                       {RANK_THREE, SUIT_HEARTS},
                                       {RANK_FOUR, SUIT_CLUBS},
                                       {RANK_FOUR, SUIT_DIAMONDS}};
    static const Card tripleWithTwo[5] = {{RANK_SEVEN, SUIT_SPADES},
                                          {RANK_SEVEN, SUIT_HEARTS},
                                          {RANK_SEVEN, SUIT_CLUBS},
                                          {RANK_NINE, SUIT_DIAMONDS},
                                          {RANK_JACK, SUIT_SPADES}};
    static const Card plane[8] = {{RANK_EIGHT, SUIT_SPADES}, {RANK_EIGHT, SUIT_HEARTS},
                                  {RANK_EIGHT, SUIT_CLUBS},  {RANK_NINE, SUIT_SPADES},
                                  {RANK_NINE, SUIT_HEARTS},  {RANK_NINE, SUIT_DIAMONDS},
                                  {RANK_FOUR, SUIT_CLUBS},   {RANK_SIX, SUIT_HEARTS}};
    static const Card bomb[4] = {{RANK_KING, SUIT_SPADES},
                                 {RANK_KING, SUIT_HEARTS},
                                 {RANK_KING, SUIT_DIAMONDS},
                                 {RANK_KING, SUIT_CLUBS}};

    switch (index) {
    case 0:
        HelpScene_PushCards(out, pair, 2);
        break;
    case 1:
        HelpScene_PushCards(out, straight, 5);
        break;
    case 2:
        HelpScene_PushCards(out, doublePair, 4);
        break;
    case 3:
        HelpScene_PushCards(out, tripleWithTwo, 5);
        break;
    case 4:
        HelpScene_PushCards(out, plane, 8);
        break;
    default:
        HelpScene_PushCards(out, bomb, 4);
        break;
    }
}

/*
 * The old lambda walked the '\n'-separated rules text, measuring each line and drawing one gold
 * bullet plus one ivory paragraph.  `strchr` is the C `find`; the two guarded calls are exactly
 * what `while (start <= text.size())` plus the `npos` check did.
 */
static float HelpScene_DrawBullets(RenderContext *context, const char *text, float x, float y,
                                   float width)
{
    TextStyle style = TextStyle_Label(15.5f, 400); /* NORMAL, matching ui::Text(15.5f) */
    const char *start = text;

    style.lineHeight = 23.0f;
    for (;;) {
        const char *end = strchr(start, '\n');
        const int length = end == NULL ? (int)strlen(start) : (int)(end - start);

        if (length > 0) {
            char line[512];
            Size size;
            Rect bullet;
            Rect paragraph;

            Str_CopyTo(line, (int)sizeof(line), start);
            if (end != NULL) {
                line[length < (int)sizeof(line) ? length : (int)sizeof(line) - 1] = '\0';
            }
            RenderContext_MeasureText(context, line, &style, width - 20.0f, &size);
            bullet = Rect_Make(x + 1.0f, y + 9.0f, 6.0f, 6.0f);
            RenderContext_FillEllipse(context, &bullet, WithAlpha(THEME_GOLD, 0.85f));
            paragraph = Rect_Make(x + 20.0f, y, width - 20.0f, size.height + 4.0f);
            RenderContext_DrawTextUtf8(context, line, &paragraph, &style, THEME_IVORY);
            y += size.height + 9.0f;
        }
        if (end == NULL) {
            break;
        }
        start = end + 1;
    }
    return y;
}

static void HelpScene_DrawPatternGallery(RenderContext *context, SpriteAtlas *atlas, float x,
                                         float y, float width)
{
    static const char *const kNames[6] = {"对子", "顺子", "连对", "三带二", "飞机", "炸弹"};
    const float cellWidth = width * 0.5f;
    const float cardW = 42.0f;
    const float cardH = cardW * 1.4f;
    const float step = cardW / 3.0f;

    for (int i = 0; i < 6; ++i) {
        Cards cards;
        const float cx = x + cellWidth * (float)(i % 2);
        const float cy = y + 78.0f * (float)(i / 2);
        Rect nameRect = Rect_Make(cx, cy + 18.0f, 56.0f, 24.0f);
        TextStyle nameStyle = TextStyle_Label(14.5f, 600); /* SEMI_BOLD */

        HelpScene_CardsOf(&cards, i);
        RenderContext_DrawTextUtf8(context, kNames[i], &nameRect, &nameStyle, THEME_GOLD);
        for (int c = 0; c < cards.count; ++c) {
            CardLook look = CardLook_Default();
            Rect cardRect = Rect_Make(cx + 58.0f + step * (float)c, cy, cardW, cardH);

            look.shadow = 0.7f;
            CardView_DrawFace(context, atlas, cards.items[c], &cardRect, &look);
        }
    }
}

static void HelpScene_OnEnter(void *user)
{
    HelpScene *scene = (HelpScene *)user;

    App_LoadCardAtlas(scene->app);
}

static void HelpScene_Update(void *user, float dt)
{
    HelpScene *scene = (HelpScene *)user;

    scene->elapsed += dt;
    ButtonGroup_UpdateAll(scene->buttons, scene->buttonCount, dt);
}

static void HelpScene_Render(void *user, RenderContext *context)
{
    const HelpScene *scene = (const HelpScene *)user;
    SpriteAtlas *atlas = App_CardAtlas(scene->app);
    float appear;
    Rect rulesTitleRect;
    TextStyle rulesTitleStyle;
    Rect sideTitleRect;
    Rect galleryTitleRect;
    float after;

    if (!SpriteAtlas_Loaded(atlas)) {
        App_LoadCardAtlas(scene->app);
        atlas = App_CardAtlas(scene->app);
    }
    RenderContext_Clear(context, THEME_ROOM);
    Widgets_DrawRoomBackground(context, Point_Make(640.0f, 300.0f));
    SceneCommon_DrawPageHeader(context, "帮助", "三人场，四十八张牌；先出完手牌的一方获胜");

    appear = EaseOutCubic(Progress(scene->elapsed, 0.0f, 0.5f));
    RenderContext_PushOpacity(context, appear);
    RenderContext_PushTranslation(context, 0.0f, (1.0f - appear) * 16.0f);

    Widgets_DrawPanel(context, &kRulesPanel, NULL);
    rulesTitleStyle = TextStyle_Kai(25.0f);
    rulesTitleStyle.weight = 700; /* BOLD, matching ui::Kai */
    rulesTitleRect = Rect_Make(kRulesPanel.x + 34.0f, kRulesPanel.y + 24.0f, 300.0f, 34.0f);
    RenderContext_DrawTextUtf8(context, "游戏规则", &rulesTitleRect, &rulesTitleStyle,
                               THEME_GOLD_LIGHT);
    HelpScene_DrawBullets(context, SharedGameRulesText(), kRulesPanel.x + 36.0f,
                          kRulesPanel.y + 76.0f, kRulesPanel.width - 72.0f);

    Widgets_DrawPanel(context, &kSidePanel, NULL);
    sideTitleRect = Rect_Make(kSidePanel.x + 34.0f, kSidePanel.y + 24.0f, 300.0f, 34.0f);
    RenderContext_DrawTextUtf8(context, "计分与托管", &sideTitleRect, &rulesTitleStyle,
                               THEME_GOLD_LIGHT);
    after = HelpScene_DrawBullets(context, HumanHelpText(), kSidePanel.x + 36.0f,
                                  kSidePanel.y + 76.0f, kSidePanel.width - 72.0f);
    Widgets_DrawHairline(context, kSidePanel.x + 20.0f, kSidePanel.x + kSidePanel.width - 20.0f,
                         after + 10.0f, 0.4f);
    galleryTitleRect = Rect_Make(kSidePanel.x + 34.0f, after + 24.0f, 300.0f, 34.0f);
    RenderContext_DrawTextUtf8(context, "牌型速览", &galleryTitleRect, &rulesTitleStyle,
                               THEME_GOLD_LIGHT);
    HelpScene_DrawPatternGallery(context, atlas, kSidePanel.x + 36.0f, after + 72.0f,
                                 kSidePanel.width - 72.0f);

    RenderContext_PopTransform(context);
    RenderContext_PopOpacity(context);
    ButtonGroup_DrawAll(context, scene->buttons, scene->buttonCount);
}

static bool HelpScene_OnMouseMove(void *user, float x, float y)
{
    HelpScene *scene = (HelpScene *)user;

    ButtonGroup_UpdateHover(scene->buttons, scene->buttonCount, x, y);
    return true;
}

static bool HelpScene_OnMouseDown(void *user, float x, float y)
{
    HelpScene *scene = (HelpScene *)user;

    if (ButtonGroup_Hit(scene->buttons, scene->buttonCount, x, y) >= 0) {
        App_PlaySound(scene->app, SOUND_CANCEL);
        App_ShowStart(scene->app);
        return true;
    }
    return false;
}

static void HelpScene_Destroy(void *user)
{
    free(user);
}

static const SceneVtbl kHelpSceneVtbl = {
    .OnEnter = HelpScene_OnEnter,
    .Update = HelpScene_Update,
    .Render = HelpScene_Render,
    .OnMouseMove = HelpScene_OnMouseMove,
    .OnMouseDown = HelpScene_OnMouseDown,
    .Destroy = HelpScene_Destroy
};

Scene HelpScene_New(void *app)
{
    HelpScene *scene = (HelpScene *)malloc(sizeof(HelpScene));
    Scene handle;

    if (scene == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    scene->app = app;
    scene->buttons[0] = SceneCommon_BackButton();
    scene->buttonCount = kButtonCapacity;
    scene->elapsed = 0.0f;
    handle.vtbl = &kHelpSceneVtbl;
    handle.user = scene;
    return handle;
}
