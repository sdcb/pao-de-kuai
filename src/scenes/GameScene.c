#include "graphics/win_compat.h"

#include "scenes/GameScene.h"

#include "app/AppApi.h"
#include "audio/SoundIds.h"
#include "core/Str.h"
#include "game/GameState.h"
#include "game/LocalAiController.h"
#include "overlays/ReturnToMenuOverlay.h"
#include "overlays/RoundResultOverlay.h"
#include "overlays/TalkBubbleOverlay.h"
#include "resources/CardAtlasData.h"
#include "scenes/GameLayout.h"
#include "stats/StatStore.h"
#include "ui/Anim.h"
#include "ui/CardView.h"
#include "ui/Theme.h"
#include "ui/Widgets.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/*
 * The game table scene.  The C++ class became a private state struct behind a hand-written
 * SceneVtbl, exactly like the four menu scenes.
 *
 * Every collection the C++ class held as std::vector/std::array is fixed storage here, sized for
 * the worst case the rules allow rather than for a round number:
 *
 *   - A three-player round deals one 48-card deck, so a hand is at most 16 cards, and
 *     `Cards` (rules/Card.h) already caps a card list at CARDS_MAX = 48.  The per-card hand
 *     animation arrays, the drag route and the "hands before sorting" snapshot all use that
 *     same CARDS_MAX bound, so no hand this scene can be handed is ever truncated.
 *   - The action button row is exactly the four buttons the scene builds.
 *   - The toast is a copy of GameState's toast text, which is capped at PDK_EVENT_TEXT_CAP.
 */
enum { kButtonCapacity = 4 };
enum { kCardCapacity = CARDS_MAX };
enum { kSeatCapacity = 3 };
enum { kToastCapacity = PDK_EVENT_TEXT_CAP };

typedef struct GameScene {
    void *app;

    GameState game;

    Button backButton;
    Button buttons[kButtonCapacity];
    int buttonCount;

    /*
     * The drag route, and the choice it settles on release.  `dragPath` holds distinct hand
     * indices only -- see GameScene_DragPush for why that is the same selection the old
     * append-only std::vector produced.
     */
    int dragPath[kCardCapacity];
    int dragPathCount;
    int dragStartCard;
    bool dragSelecting;
    bool dragMoved;
    int hoverCard;

    /* Per-card hand lift/hover, with `handAnimationCount` entries valid for the current hand. */
    float handLift[kCardCapacity];
    float handHover[kCardCapacity];
    int handAnimationCount;

    bool recordedRound;
    bool roundResultPending;
    bool mock;
    bool midgameMock;
    bool mockPlayed;
    bool mockHinted;
    bool actionButtonsDirty;
    bool lastInteractionReady;
    bool handsSorted;

    int todayScores[kSeatCapacity];
    Cards handsBeforeSort[kSeatCapacity];
    float passTimers[kSeatCapacity];

    float time;
    float dealElapsed;
    int dealSoundCount;
    float sortAnimation;
    float playAnimation;
    float bombAnimation;
    float roundResultDelay;

    char toast[kToastCapacity];
    float toastAge;

    PlayerId lastAnimatedPlayer;
} GameScene;

static const float kPi = 3.14159265f;
static const float kDealCardInterval = 1.0f / 6.0f;
enum { kDealSoundCount = 16 };
enum { kFullHandCardCount = 16 };
static const float kSortAnimationSpeed = 2.4f;
static const float kRoundResultDelaySeconds = 1.2f;
static const float kPlayerCardWidth = 105.0f;
static const float kPlayerCardHeight = 147.0f;
static const float kPlayedCardWidth = 92.0f;
static const float kPlayedCardTop = 220.0f;
static const float kHoverLift = 7.0f;
static const float kPassChipSeconds = 1.6f;
static const float kToastSeconds = 4.5f;
static const Point kDealCenter = {640.0f, 300.0f};
static const Point kPlayerPlaySource = {640.0f, 640.0f};

/* ---- small helpers --------------------------------------------------- */

static bool GameScene_PathContains(const int *values, int count, int index)
{
    for (int i = 0; i < count; ++i) {
        if (values[i] == index) {
            return true;
        }
    }
    return false;
}

/*
 * Records one card on the drag route.  The C++ std::vector appended on every move to a different
 * card, so a mouse wagging between two cards grew it without bound; nothing ever read the order or
 * the repeats, though -- GameState_SelectBestPatternFromDraggedCards turns the route into a set of
 * hand indices before it does anything.  Skipping a card already on the route therefore selects
 * exactly the same cards (the `dragMoved_ && dragPath_.size() > 1` branch below is likewise
 * unchanged: size > 1 iff two distinct cards were visited), and it bounds the route by the
 * distinct cards in the hand instead of by how long the mouse is dragged.
 */
static void GameScene_DragPush(GameScene *scene, int card)
{
    if (scene->dragPathCount >= kCardCapacity ||
        GameScene_PathContains(scene->dragPath, scene->dragPathCount, card)) {
        return;
    }
    scene->dragPath[scene->dragPathCount] = card;
    scene->dragPathCount++;
}

/* mainX is the atlas-defined safe visible strip; stepping by this scaled width prevents the next
 * card from revealing the large right-side suit. */
static float GameScene_VisibleStepForWidth(float cardWidth)
{
    const CardAtlasInfo *info = GetCardAtlasInfo();

    return cardWidth * (float)info->mainX / (float)info->cardWidth;
}

static float GameScene_CardRowWidth(int count, float cardWidth)
{
    if (count <= 0) {
        return 0.0f;
    }
    return cardWidth + (float)(count - 1) * GameScene_VisibleStepForWidth(cardWidth);
}

/* Moves a dealt card from the centre pile along a shallow arc; returns flight progress. */
static float GameScene_DealFlight(Rect *rect, float *rotation, int index, float elapsed)
{
    const float t =
        Clamp01((elapsed - (float)index * kDealCardInterval) / (kDealCardInterval * 1.5f));
    const float e = EaseOutCubic(t);
    const float startRotation = (float)((index * 37) % 29 - 14);

    rect->x = Lerp(kDealCenter.x - rect->width * 0.5f, rect->x, e);
    rect->y = Lerp(kDealCenter.y - rect->height * 0.5f, rect->y, e) - sinf(t * kPi) * 46.0f;
    *rotation = Lerp(startRotation, 0.0f, e);
    return t;
}

/* The old SignedScore built a std::string with core::AppendNumber; Str is the C replacement and
 * the caller frees it. */
static void GameScene_SignedScore(Str *out, int score)
{
    if (score > 0) {
        Str_Append(out, "+");
    }
    Str_AppendNumber(out, score);
}

/* The old lambda compared the setting with "strong". */
static LocalAiKind GameScene_AiKind(const char *selection)
{
    return strcmp(selection, "strong") == 0 ? LOCAL_AI_STRONG : LOCAL_AI_BASIC;
}

/* ---- geometry -------------------------------------------------------- */

static Rect GameScene_CardRectFor(int index, int count)
{
    const float step = GameScene_VisibleStepForWidth(kPlayerCardWidth);
    const float totalW = GameScene_CardRowWidth(count, kPlayerCardWidth);
    const float startX = 640.0f - totalW * 0.5f;

    return Rect_Make(startX + (float)index * step,
                     (float)GAME_LAYOUT_HAND_BOTTOM - kPlayerCardHeight, kPlayerCardWidth,
                     kPlayerCardHeight);
}

static Rect GameScene_CardRect(const GameScene *scene, int index)
{
    const Cards *hand = &GameState_Players(&scene->game)[PLAYER_HUMAN].hand;

    return GameScene_CardRectFor(index, hand->count);
}

static Rect GameScene_AiCardRectFor(PlayerId player, int index, int count)
{
    const Rect info = GameLayout_AiInfoArea(player);
    const float cardH = (float)GAME_LAYOUT_AI_CARD_HEIGHT;
    const float cardW = cardH / 1.4f;
    const float step = (info.width - cardW) / (float)(kFullHandCardCount - 1);
    const float y = info.y + 36.0f;

    if (player == PLAYER_AI2) {
        /* Right-aligned so the remaining cards stay next to AI2's avatar. */
        const float right = info.x + info.width - cardW;

        return Rect_Make(right - (float)(count - 1 - index) * step, y, cardW, cardH);
    }
    return Rect_Make(info.x + (float)index * step, y, cardW, cardH);
}

static int GameScene_HitPlayerCard(const GameScene *scene, float x, float y)
{
    const Cards *hand = &GameState_Players(&scene->game)[PLAYER_HUMAN].hand;
    const uint64_t selected = GameState_SelectedMask(&scene->game);

    for (int i = hand->count - 1; i >= 0; --i) {
        Rect rect = GameScene_CardRect(scene, i);

        if (((selected >> i) & 1u) != 0u) {
            rect.y -= (float)GAME_LAYOUT_SELECTED_LIFT;
        }
        if (Rect_Contains(&rect, x, y)) {
            return i;
        }
    }
    return -1;
}

static bool GameScene_InteractionReady(const GameScene *scene)
{
    return scene->handsSorted && scene->sortAnimation <= 0.0f &&
           !GameState_IsRoundOver(&scene->game);
}

/* ---- the action button row ------------------------------------------- */

static void GameScene_LayoutActionButtons(GameScene *scene)
{
    const float gap = 12.0f;
    float total = -gap;
    float x;

    for (int i = 0; i < scene->buttonCount; ++i) {
        total += scene->buttons[i].rect.width + gap;
    }
    x = 640.0f - total * 0.5f;
    for (int i = 0; i < scene->buttonCount; ++i) {
        scene->buttons[i].rect.x = x;
        scene->buttons[i].rect.y = 482.0f;
        x += scene->buttons[i].rect.width + gap;
    }
}

static void GameScene_UpdateActionButtons(GameScene *scene)
{
    GameState *game = &scene->game;
    const bool ready = GameScene_InteractionReady(scene);
    /* Render and hover paths read these cached values only; state-changing callbacks mark the
     * cache dirty when a recompute is needed. */
    const bool showActionButtons = !ready || GameState_IsHumanTurn(game);

    GameScene_LayoutActionButtons(scene);
    for (int i = 0; i < scene->buttonCount; ++i) {
        scene->buttons[i].visible = showActionButtons && !GameState_IsRoundOver(game);
        if (!scene->buttons[i].visible) {
            scene->buttons[i].hover = false;
        }
    }
    Str_CopyTo(scene->buttons[0].text, PDK_BUTTON_TEXT_CAP,
               GameState_Autoplay(game) ? "取消托管" : "托管");
    scene->buttons[0].enabled = ready;
    scene->buttons[1].enabled = ready && GameState_CanCurrentPlayerPass(game);
    scene->buttons[2].enabled = ready && GameState_IsHumanTurn(game);
    scene->buttons[3].enabled =
        ready && GameState_IsHumanTurn(game) && GameState_SelectedMask(game) != 0u;
    scene->lastInteractionReady = ready;
    scene->actionButtonsDirty = false;
}

/* ---- table ----------------------------------------------------------- */

static void GameScene_DrawTable(RenderContext *context)
{
    const Rect table = GAME_LAYOUT_TABLE;
    const Point center = Point_Make(table.x + table.width * 0.5f, table.y + table.height * 0.5f);
    const float rx = table.width * 0.5f;
    const float ry = table.height * 0.5f;
    const D2D1_COLOR_F black = ColorF_Make(0.0f, 0.0f, 0.0f, 1.0f);
    GradientStop stops[3];
    Rect rail;
    Rect shape;

    /* Drop shadow under the whole table. */
    shape = Rect_Make(center.x - rx - 40.0f, center.y - ry - 26.0f, (rx + 40.0f) * 2.0f,
                      (ry + 40.0f) * 2.0f);
    stops[0] = GradientStop_Make(0.0f, WithAlpha(black, 0.6f));
    stops[1] = GradientStop_Make(0.86f, WithAlpha(black, 0.5f));
    stops[2] = GradientStop_Make(1.0f, WithAlpha(black, 0.0f));
    RenderContext_FillEllipseBrush(
        context, &shape,
        RenderContext_Radial(context, Point_Make(center.x, center.y + 14.0f), rx + 40.0f,
                             ry + 40.0f, stops, 3));

    /* Padded leather rail with a gold inlay. */
    rail = Rect_Make(table.x - 16.0f, table.y - 16.0f, table.width + 32.0f, table.height + 32.0f);
    stops[0] = GradientStop_Make(0.0f, Rgb(0x3A2A1C, 1.0f));
    stops[1] = GradientStop_Make(0.5f, Rgb(0x21170F, 1.0f));
    stops[2] = GradientStop_Make(1.0f, Rgb(0x120C08, 1.0f));
    RenderContext_FillEllipseBrush(
        context, &rail,
        RenderContext_Linear(context, Point_Make(0.0f, rail.y),
                             Point_Make(0.0f, rail.y + rail.height), stops, 3));
    shape = Rect_Make(rail.x + 2.0f, rail.y + 2.0f, rail.width - 4.0f, rail.height - 4.0f);
    RenderContext_StrokeEllipse(context, &shape, WithAlpha(THEME_GOLD_LIGHT, 0.10f), 2.0f);
    shape = Rect_Make(table.x - 8.0f, table.y - 8.0f, table.width + 16.0f, table.height + 16.0f);
    RenderContext_StrokeEllipse(context, &shape, WithAlpha(THEME_GOLD, 0.28f), 1.0f);

    /* Felt, lit from slightly above centre. */
    stops[0] = GradientStop_Make(0.0f, THEME_FELT_LIGHT);
    stops[1] = GradientStop_Make(0.55f, THEME_FELT);
    stops[2] = GradientStop_Make(1.0f, THEME_FELT_DEEP);
    RenderContext_FillEllipseBrush(
        context, &table,
        RenderContext_Radial(context, Point_Make(center.x, center.y - 30.0f), rx * 1.02f,
                             ry * 1.15f, stops, 3));
    RenderContext_PushOpacity(context, 0.32f);
    RenderContext_FillEllipseBrush(context, &table, RenderContext_FeltBrush(context));
    RenderContext_PopOpacity(context);
    shape = Rect_Make(table.x + 9.0f, table.y + 9.0f, table.width - 18.0f, table.height - 18.0f);
    RenderContext_StrokeEllipse(context, &shape, WithAlpha(black, 0.16f), 18.0f);
    shape = Rect_Make(table.x + 3.0f, table.y + 3.0f, table.width - 6.0f, table.height - 6.0f);
    RenderContext_StrokeEllipse(context, &shape, WithAlpha(black, 0.28f), 6.0f);
    shape = Rect_Make(table.x - 0.5f, table.y - 0.5f, table.width + 1.0f, table.height + 1.0f);
    RenderContext_StrokeEllipse(context, &shape, WithAlpha(THEME_GOLD, 0.78f), 1.6f);
    /* Faint play-zone line. */
    shape = Rect_Make(table.x + 70.0f, table.y + 58.0f, table.width - 140.0f, table.height - 116.0f);
    RenderContext_StrokeEllipse(context, &shape, WithAlpha(THEME_GOLD, 0.08f), 1.0f);
}

static void GameScene_DrawTurnChip(GameScene *scene, RenderContext *context)
{
    GameState *game = &scene->game;
    const Point anchor = Point_Make(640.0f, 44.0f);
    Str text;
    const char *label;
    bool highlight = false;
    ChipStyle style = ChipStyle_Default();

    Str_Init(&text);
    if (GameState_IsRoundOver(game)) {
        Str_Append(&text, "本局结束");
    } else if (!scene->handsSorted) {
        Str_Append(&text, "发牌中");
    } else if (GameState_IsHumanTurn(game)) {
        Str_Append(&text, "轮到你出牌");
        highlight = true;
    } else {
        Str_Append(&text,
                   GameState_Players(game)[PlayerIndex(GameState_CurrentPlayer(game))].name);
        Str_Append(&text, " 出牌中");
    }
    label = Str_CStr(&text);

    style.fontSize = 16.0f;
    style.height = 34.0f;
    style.padX = 22.0f;
    style.weight = 600; /* DWRITE_FONT_WEIGHT_SEMI_BOLD */
    if (highlight) {
        const float pulse = 0.5f + 0.5f * sinf(scene->time * 4.0f);
        const Rect rect = Widgets_ChipRect(context, anchor, UI_ANCHOR_CENTER, label, &style);

        RenderContext_DrawShadow(context, &rect, 9.0f,
                                 WithAlpha(THEME_GOLD, 0.25f + 0.2f * pulse));
        style.fill = WithAlpha(THEME_GOLD_DEEP, 0.35f);
        style.stroke = WithAlpha(THEME_GOLD_LIGHT, 0.85f);
        style.text = THEME_GOLD_LIGHT;
    } else {
        style.text = THEME_MUTED;
    }
    Widgets_DrawChip(context, anchor, UI_ANCHOR_CENTER, label, &style);
    Str_Free(&text);
}

static void GameScene_DrawAiSeat(GameScene *scene, RenderContext *context, PlayerId player)
{
    GameState *game = &scene->game;
    const PlayerState *seat = &GameState_Players(game)[PlayerIndex(player)];
    const Rect plate = GameLayout_PlateFor(player);
    const bool active =
        scene->handsSorted && !GameState_IsRoundOver(game) && GameState_CurrentPlayer(game) == player;
    PanelStyle panel = PanelStyle_Default();
    Rect avatar;
    Rect info;
    Rect shape;
    char label[PDK_BUTTON_TEXT_CAP];
    int count;
    ChipStyle countChip = ChipStyle_Default();
    Str countText;
    const char *countLabel;
    TextStyle nameStyle;
    TextStyle thinkingStyle;
    TextStyle scoreStyle;
    int today;

    panel.radius = 20.0f;
    panel.ornament = false;
    panel.fillAlpha = 0.9f;
    Widgets_DrawPanel(context, &plate, &panel);
    if (active) {
        const float pulse = 0.5f + 0.5f * sinf(scene->time * 4.2f);

        shape = Rect_Make(plate.x + 0.5f, plate.y + 0.5f, plate.width - 1.0f, plate.height - 1.0f);
        RenderContext_StrokeRoundedRect(context, &shape, panel.radius,
                                        WithAlpha(THEME_GOLD_LIGHT, 0.35f + 0.3f * pulse), 1.6f);
    }

    avatar = GameLayout_AvatarRect(player);
    Widgets_AvatarLabel(seat->name, label, (int)sizeof(label));
    Widgets_DrawAvatar(context, &avatar, label, active, scene->time);

    count = seat->hand.count;
    countChip.fontSize = 13.0f;
    countChip.height = 22.0f;
    countChip.padX = 10.0f;
    Str_Init(&countText);
    Str_AppendNumber(&countText, count);
    Str_Append(&countText, " 张");
    countLabel = Str_CStr(&countText);
    if (!GameState_IsRoundOver(game) && scene->handsSorted && count > 0 && count <= 2) {
        const Point anchor =
            Point_Make(avatar.x + avatar.width * 0.5f, avatar.y + avatar.height + 16.0f);

        countLabel = count == 1 ? "报单" : "报双";
        countChip.fill = THEME_CINNABAR;
        countChip.stroke = WithAlpha(THEME_CINNABAR_LIGHT, 0.8f);
        countChip.weight = 600; /* DWRITE_FONT_WEIGHT_SEMI_BOLD */
        shape = Widgets_ChipRect(context, anchor, UI_ANCHOR_CENTER, countLabel, &countChip);
        RenderContext_DrawShadow(context, &shape, 7.0f,
                                 WithAlpha(THEME_CINNABAR, 0.45f + 0.25f * sinf(scene->time * 6.0f)));
    }
    Widgets_DrawChip(context, Point_Make(avatar.x + avatar.width * 0.5f,
                                         avatar.y + avatar.height + 16.0f),
                     UI_ANCHOR_CENTER, countLabel, &countChip);
    Str_Free(&countText);

    info = GameLayout_AiInfoArea(player);
    nameStyle = TextStyle_Label(17.5f, 600 /* SEMI_BOLD */);
    nameStyle.wrap = false;
    nameStyle.ellipsis = true;
    shape = Rect_Make(info.x, info.y, info.width - 96.0f, 26.0f);
    RenderContext_DrawTextUtf8(context, seat->name, &shape, &nameStyle, THEME_IVORY);
    if (active) {
        Size measured;
        Str thinking;
        float nameWidth;
        int dots;

        RenderContext_MeasureText(context, seat->name, &nameStyle, 4096.0f, &measured);
        nameWidth = PDK_MIN(info.width - 96.0f, measured.width);
        Str_Init(&thinking);
        Str_Append(&thinking, "思考中");
        dots = (int)(scene->time * 3.0f) % 4;
        for (int i = 0; i < dots; ++i) {
            Str_AppendChar(&thinking, '.');
        }
        thinkingStyle = TextStyle_Label(13.5f, 400);
        shape = Rect_Make(info.x + nameWidth + 10.0f, info.y + 3.0f, 120.0f, 22.0f);
        RenderContext_DrawTextUtf8(context, Str_CStr(&thinking), &shape, &thinkingStyle,
                                   THEME_GOLD);
        Str_Free(&thinking);
    }
    scoreStyle = TextStyle_Label(13.5f, 400);
    scoreStyle.align = 2; /* DWRITE_TEXT_ALIGNMENT_TRAILING */
    scoreStyle.wrap = false;
    today = scene->todayScores[PlayerIndex(player)];
    {
        Str todayText;

        Str_Init(&todayText);
        Str_Append(&todayText, "今日 ");
        GameScene_SignedScore(&todayText, today);
        shape = Rect_Make(info.x + info.width - 96.0f, info.y + 3.0f, 96.0f, 22.0f);
        RenderContext_DrawTextUtf8(context, Str_CStr(&todayText), &shape, &scoreStyle,
                                   ScoreColor(today));
        Str_Free(&todayText);
    }

    for (int i = 0; i < count; ++i) {
        Rect target = GameScene_AiCardRectFor(player, i, count);
        CardLook look = CardLook_Default();

        look.shadow = 0.8f;
        if (!scene->handsSorted) {
            if (GameScene_DealFlight(&target, &look.rotation, i, scene->dealElapsed) <= 0.0f) {
                continue;
            }
        } else if (scene->sortAnimation > 0.0f) {
            const Cards *oldHand = &scene->handsBeforeSort[PlayerIndex(player)];
            const int oldIndex = Cards_IndexOf(oldHand, seat->hand.items[i]);

            if (oldIndex >= 0) {
                /* AI cards stay face-down, but their backs still move from the dealt order to
                 * the sorted order so all hands feel consistent. */
                const Rect from = GameScene_AiCardRectFor(player, oldIndex, oldHand->count);
                const float t = EaseInOutSine(1.0f - scene->sortAnimation);

                target.x = Lerp(from.x, target.x, t);
                target.y = Lerp(from.y, target.y, t);
            }
        }
        /* After the round ends the result overlay is intentionally delayed, so reveal the AI
         * leftovers here to let the player inspect what was held. */
        if (GameState_IsRoundOver(game)) {
            CardView_DrawFace(context, App_CardAtlas(scene->app), seat->hand.items[i], &target,
                              &look);
        } else {
            CardView_DrawBack(context, App_CardAtlas(scene->app), &target, &look);
        }
    }
}

static void GameScene_DrawPlayerPlate(GameScene *scene, RenderContext *context)
{
    AppSettings settings;
    const Rect plate = GAME_LAYOUT_PLAYER_PLATE;
    const bool active = scene->handsSorted && GameState_IsHumanTurn(&scene->game);
    PanelStyle panel = PanelStyle_Default();
    Rect avatar;
    Rect shape;
    TextStyle nameStyle;
    TextStyle todayStyle;
    char label[PDK_BUTTON_TEXT_CAP];
    Str todayText;

    App_GetSettings(scene->app, &settings);
    panel.radius = 20.0f;
    panel.ornament = false;
    panel.fillAlpha = 0.88f;
    Widgets_DrawPanel(context, &plate, &panel);
    avatar = GameLayout_AvatarRect(PLAYER_HUMAN);
    Widgets_AvatarLabel(settings.playerName, label, (int)sizeof(label));
    Widgets_DrawAvatar(context, &avatar, label, active, scene->time);

    nameStyle = TextStyle_Label(17.5f, 600 /* SEMI_BOLD */);
    nameStyle.wrap = false;
    nameStyle.ellipsis = true;
    shape = Rect_Make(plate.x + 80.0f, plate.y + 12.0f, 110.0f, 26.0f);
    RenderContext_DrawTextUtf8(context, settings.playerName, &shape, &nameStyle, THEME_IVORY);
    todayStyle = TextStyle_Label(14.5f, 400);
    Str_Init(&todayText);
    Str_Append(&todayText, "今日 ");
    GameScene_SignedScore(&todayText, scene->todayScores[0]);
    shape = Rect_Make(plate.x + 80.0f, plate.y + 42.0f, 160.0f, 22.0f);
    RenderContext_DrawTextUtf8(context, Str_CStr(&todayText), &shape, &todayStyle,
                               ScoreColor(scene->todayScores[0]));
    Str_Free(&todayText);
    if (GameState_Autoplay(&scene->game)) {
        ChipStyle chip = ChipStyle_Default();

        chip.fontSize = 12.5f;
        chip.height = 22.0f;
        chip.padX = 9.0f;
        chip.fill = WithAlpha(THEME_GOLD_DEEP, 0.5f);
        chip.stroke = WithAlpha(THEME_GOLD, 0.8f);
        chip.text = THEME_GOLD_LIGHT;
        Widgets_DrawChip(context, Point_Make(plate.x + plate.width - 14.0f, plate.y + 25.0f),
                         UI_ANCHOR_RIGHT, "托管中", &chip);
    }
}

static void GameScene_DrawPlayerHand(GameScene *scene, RenderContext *context)
{
    GameState *game = &scene->game;
    const Cards *hand = &GameState_Players(game)[PLAYER_HUMAN].hand;
    const float glowPulse = 0.72f + 0.28f * sinf(scene->time * 5.0f);
    const uint64_t selected = GameState_SelectedMask(game);
    const uint64_t hint = GameState_HintMask(game);
    SpriteAtlas *atlas = App_CardAtlas(scene->app);

    for (int i = 0; i < hand->count; ++i) {
        const Card card = hand->items[i];
        Rect rect = GameScene_CardRect(scene, i);
        CardLook look = CardLook_Default();

        if (!scene->handsSorted) {
            if (GameScene_DealFlight(&rect, &look.rotation, i, scene->dealElapsed) <= 0.0f) {
                continue;
            }
            CardView_DrawFace(context, atlas, card, &rect, &look);
            continue;
        }
        if (scene->sortAnimation > 0.0f) {
            const Cards *oldHand = &scene->handsBeforeSort[PlayerIndex(PLAYER_HUMAN)];
            const int oldIndex = Cards_IndexOf(oldHand, card);

            if (oldIndex >= 0) {
                const Rect from = GameScene_CardRectFor(oldIndex, oldHand->count);
                const float t = EaseInOutSine(1.0f - scene->sortAnimation);

                rect.x = Lerp(from.x, rect.x, t);
                rect.y = Lerp(from.y, rect.y, t) - sinf(t * kPi) * 16.0f;
            }
        }
        look.lift = i < scene->handAnimationCount ? scene->handLift[i] : 0.0f;
        look.hover = i < scene->handAnimationCount ? scene->handHover[i] : 0.0f;
        look.selected = ((selected >> i) & 1u) != 0u;
        if (scene->dragSelecting &&
            GameScene_PathContains(scene->dragPath, scene->dragPathCount, i)) {
            look.glow = 0.85f;
            look.glowColor = THEME_IVORY;
        } else if (((hint >> i) & 1u) != 0u) {
            look.glow = glowPulse;
        }
        CardView_DrawFace(context, atlas, card, &rect, &look);
    }
}

static void GameScene_DrawPlayedCards(GameScene *scene, RenderContext *context)
{
    GameState *game = &scene->game;
    const Cards *cards = GameState_LastCards(game);
    const HandPattern *pattern = GameState_LastPattern(game);
    const float cardW = kPlayedCardWidth;
    const float cardH = cardW * 1.4f;
    const float step = GameScene_VisibleStepForWidth(cardW);
    const float totalW = GameScene_CardRowWidth(cards->count, cardW);
    const float left = 640.0f - totalW * 0.5f;
    const float progress = 1.0f - scene->playAnimation;
    Point source = kPlayerPlaySource;
    float sourceRotation = 0.0f;

    if (cards->count == 0) {
        TextStyle mark = TextStyle_Centered(TextStyle_Kai(104.0f));
        Rect markRect = Rect_Make(340.0f, 236.0f, 600.0f, 120.0f);

        mark.weight = 700; /* BOLD, matching ui::Kai */
        mark.wrap = false;
        RenderContext_DrawTextUtf8(context, "跑得快", &markRect, &mark,
                                   WithAlpha(THEME_GOLD_LIGHT, 0.06f));
        if (scene->handsSorted && !GameState_IsRoundOver(game)) {
            TextStyle waiting = TextStyle_Centered(TextStyle_Label(14.5f, 400));
            Rect waitingRect = Rect_Make(340.0f, 350.0f, 600.0f, 24.0f);

            RenderContext_DrawTextUtf8(context, "等待出牌", &waitingRect, &waiting,
                                       WithAlpha(THEME_MUTED, 0.6f));
        }
        return;
    }
    if (scene->lastAnimatedPlayer != PLAYER_HUMAN) {
        source = GameLayout_AvatarCenter(scene->lastAnimatedPlayer);
        sourceRotation = scene->lastAnimatedPlayer == PLAYER_AI1 ? -18.0f : 18.0f;
    }
    for (int i = 0; i < cards->count; ++i) {
        const Rect finalRect = Rect_Make(left + (float)i * step, kPlayedCardTop, cardW, cardH);
        Rect rect = finalRect;
        CardLook look = CardLook_Default();

        if (scene->playAnimation > 0.0f) {
            const float t = Clamp01((progress - (float)i * 0.012f) / 0.8f);
            const float e = EaseOutCubic(t);
            const float scale = Lerp(0.5f, 1.0f, EaseOutBackWith(t, 1.3f));
            const float cx = Lerp(source.x, finalRect.x + cardW * 0.5f, e);
            const float cy = Lerp(source.y, finalRect.y + cardH * 0.5f, e);

            rect.width = cardW * scale;
            rect.height = cardH * scale;
            rect.x = cx - rect.width * 0.5f;
            rect.y = cy - rect.height * 0.5f;
            look.rotation = Lerp(sourceRotation, 0.0f, e);
            look.lift = (1.0f - e) * 18.0f;
        }
        CardView_DrawFace(context, App_CardAtlas(scene->app), cards->items[i], &rect, &look);
    }

    if (pattern != NULL) {
        const float appear = EaseOutCubic(Progress(progress, 0.5f, 0.5f));
        ChipStyle chip = ChipStyle_Default();
        Str label;
        char description[PATTERN_REASON_CAP];

        RenderContext_PushOpacity(context, appear);
        chip.fontSize = 14.5f;
        chip.height = 30.0f;
        chip.padX = 16.0f;
        Str_Init(&label);
        Str_Append(&label,
                   GameState_Players(game)[PlayerIndex(GameState_LastMovePlayer(game))].name);
        Str_Append(&label, "  ·  ");
        PatternDescription(pattern, description, PATTERN_REASON_CAP);
        Str_Append(&label, description);
        Widgets_DrawChip(context, Point_Make(640.0f, kPlayedCardTop + cardH + 28.0f),
                         UI_ANCHOR_CENTER, Str_CStr(&label), &chip);
        Str_Free(&label);
        RenderContext_PopOpacity(context);
    }
}

static void GameScene_DrawPassChips(GameScene *scene, RenderContext *context)
{
    static const Point kAnchors[kSeatCapacity] = {
        {640.0f, 504.0f}, {300.0f, 232.0f}, {980.0f, 232.0f}};
    ChipStyle chip = ChipStyle_Default();

    chip.fontSize = 17.0f;
    chip.height = 38.0f;
    chip.padX = 24.0f;
    chip.weight = 600; /* DWRITE_FONT_WEIGHT_SEMI_BOLD */
    chip.fill = WithAlpha(THEME_INK, 0.88f);
    chip.stroke = WithAlpha(THEME_IVORY, 0.35f);
    for (int i = 0; i < kSeatCapacity; ++i) {
        const float timer = scene->passTimers[i];
        const float age = kPassChipSeconds - timer;
        const float alpha = Clamp01(age * 6.0f) * Clamp01(timer * 3.0f);
        const Point anchor =
            Point_Make(kAnchors[i].x, kAnchors[i].y - EaseOutCubic(Clamp01(age * 3.0f)) * 10.0f);
        Rect rect;

        if (timer <= 0.0f) {
            continue;
        }
        rect = Widgets_ChipRect(context, anchor, UI_ANCHOR_CENTER, "不要", &chip);
        RenderContext_PushOpacity(context, alpha);
        RenderContext_DrawShadow(context, &rect, 8.0f, ColorF_Make(0.0f, 0.0f, 0.0f, 0.5f));
        Widgets_DrawChip(context, anchor, UI_ANCHOR_CENTER, "不要", &chip);
        RenderContext_PopOpacity(context);
    }
}

static void GameScene_DrawToast(GameScene *scene, RenderContext *context)
{
    float alpha;
    ChipStyle chip = ChipStyle_Default();
    Point anchor;

    if (scene->toast[0] == '\0' || scene->toastAge >= kToastSeconds) {
        return;
    }
    alpha = Clamp01(scene->toastAge * 5.0f) * Clamp01((kToastSeconds - scene->toastAge) * 2.0f);
    RenderContext_PushOpacity(context, alpha);
    chip.fontSize = 15.0f;
    chip.height = 34.0f;
    chip.padX = 20.0f;
    chip.fill = WithAlpha(THEME_INK, 0.9f);
    chip.stroke = WithAlpha(THEME_GOLD, 0.4f);
    anchor = Point_Make(640.0f,
                        446.0f + (1.0f - EaseOutCubic(Clamp01(scene->toastAge * 4.0f))) * 8.0f);
    Widgets_DrawChip(context, anchor, UI_ANCHOR_CENTER, scene->toast, &chip);
    RenderContext_PopOpacity(context);
}

static void GameScene_DrawDealPile(GameScene *scene, RenderContext *context)
{
    int remaining;
    int layers;

    if (scene->handsSorted) {
        return;
    }
    remaining = PDK_MAX(0, kDealSoundCount - (int)(scene->dealElapsed / kDealCardInterval));
    layers = PDK_MIN(4, (remaining + 3) / 4);
    for (int i = 0; i < layers; ++i) {
        const float offset = (float)(layers - 1 - i) * 2.5f;
        const Rect rect = Rect_Make(kDealCenter.x - kPlayerCardWidth * 0.5f + offset,
                                    kDealCenter.y - kPlayerCardHeight * 0.5f + offset,
                                    kPlayerCardWidth, kPlayerCardHeight);
        CardLook look = CardLook_Default();

        look.rotation = (float)(i % 2 == 0 ? -2 : 3);
        CardView_DrawBack(context, App_CardAtlas(scene->app), &rect, &look);
    }
}

static void GameScene_DrawBombEffect(GameScene *scene, RenderContext *context)
{
    const Point center = Point_Make(640.0f, 290.0f);
    GradientStop stops[3];
    Rect shape;
    float ring;
    float ring2;

    if (scene->bombAnimation <= 0.0f) {
        return;
    }
    {
        const float p = 1.0f - scene->bombAnimation;
        const float fade = scene->bombAnimation;
        float stamp;
        float sealAlpha;

        shape = Rect_Make(0.0f, 0.0f, LogicalWidth, LogicalHeight);
        stops[0] = GradientStop_Make(0.0f, Rgb(0xFFD58A, 0.5f * fade * fade * fade));
        stops[1] = GradientStop_Make(0.45f, Rgb(0xC23B2E, 0.18f * fade * fade));
        stops[2] = GradientStop_Make(1.0f, Rgb(0xC23B2E, 0.0f));
        RenderContext_FillRectBrush(
            context, &shape,
            RenderContext_Radial(context, center, 820.0f, 600.0f, stops, 3));

        ring = 60.0f + 640.0f * EaseOutCubic(p);
        shape = Rect_Make(center.x - ring, center.y - ring * 0.62f, ring * 2.0f, ring * 1.24f);
        RenderContext_StrokeEllipse(context, &shape, WithAlpha(THEME_GOLD_LIGHT, fade),
                                    1.0f + 6.0f * fade);
        ring2 = 40.0f + 480.0f * EaseOutCubic(Clamp01(p * 1.25f - 0.1f));
        shape = Rect_Make(center.x - ring2, center.y - ring2 * 0.62f, ring2 * 2.0f, ring2 * 1.24f);
        RenderContext_StrokeEllipse(context, &shape, WithAlpha(THEME_CINNABAR_LIGHT, fade * 0.8f),
                                    1.0f + 4.0f * fade);

        stamp = EaseOutBackWith(Clamp01(p / 0.22f), 1.6f);
        sealAlpha = Clamp01(p * 10.0f) * Clamp01((1.0f - p) / 0.35f);
        RenderContext_PushOpacity(context, sealAlpha);
        RenderContext_PushScale(context, Lerp(2.3f, 1.0f, stamp), center);
        Widgets_DrawSeal(context, center, 128.0f, "炸", THEME_CINNABAR, -8.0f, 84.0f);
        RenderContext_PopTransform(context);
        RenderContext_PopOpacity(context);
    }
}

/* ---- the round ------------------------------------------------------- */

static void GameScene_InitializeExternalAi(GameScene *scene)
{
    AppSettings settings;
    LocalAiController *controller;

    if (scene->mock || App_ViewerMode(scene->app)) {
        GameState_SetExternalAiControllers(&scene->game, NULL, 0);
        return;
    }

    controller = LocalAiController_Create();
    App_GetSettings(scene->app, &settings);
    LocalAiController_SetStrategy(controller, PLAYER_AI1, GameScene_AiKind(settings.ai1));
    LocalAiController_SetStrategy(controller, PLAYER_AI2, GameScene_AiKind(settings.ai2));
    /* GameState takes ownership from here. */
    GameState_SetExternalAiController(&scene->game, LocalAiController_Interface(controller));
}

static void GameScene_StartNextRound(GameScene *scene)
{
    GameState *game = &scene->game;
    AppSettings settings;

    App_GetSettings(scene->app, &settings);
    GameState_SetRoundTraceEnabled(game, settings.roundTraceEnabled);
    GameState_StartNewRound(game, settings.playerName, scene->mock ? 20260606u : 0u);
    scene->recordedRound = false;
    scene->roundResultPending = false;
    for (int i = 0; i < kSeatCapacity; ++i) {
        scene->handsBeforeSort[i] = GameState_Players(game)[i].hand;
    }
    scene->handsSorted = false;
    scene->dealElapsed = 0.0f;
    scene->sortAnimation = 0.0f;
    scene->playAnimation = 0.0f;
    scene->bombAnimation = 0.0f;
    scene->roundResultDelay = 0.0f;
    scene->dealSoundCount = 0;
    scene->dragSelecting = false;
    scene->dragMoved = false;
    scene->hoverCard = -1;
    scene->backButton.hover = false;
    scene->dragStartCard = -1;
    scene->dragPathCount = 0;
    scene->handAnimationCount = 0;
    memset(scene->handLift, 0, sizeof(scene->handLift));
    memset(scene->handHover, 0, sizeof(scene->handHover));
    scene->passTimers[0] = 0.0f;
    scene->passTimers[1] = 0.0f;
    scene->passTimers[2] = 0.0f;
    scene->lastAnimatedPlayer = PLAYER_HUMAN;
    if (scene->midgameMock) {
        GameState_SortHands(game);
        scene->handsSorted = true;
        scene->dealSoundCount = kDealSoundCount;
        scene->dealElapsed = (float)kDealSoundCount * kDealCardInterval + 1.0f;
    }
    scene->actionButtonsDirty = true;
    scene->lastInteractionReady = GameScene_InteractionReady(scene);
    GameScene_UpdateActionButtons(scene);
}

static void GameScene_UpdateHandAnimation(GameScene *scene, float dt)
{
    const Cards *hand = &GameState_Players(&scene->game)[PLAYER_HUMAN].hand;
    const uint64_t selected = GameState_SelectedMask(&scene->game);
    const bool ready = GameScene_InteractionReady(scene);

    if (scene->handAnimationCount != hand->count) {
        for (int i = 0; i < kCardCapacity; ++i) {
            scene->handLift[i] = 0.0f;
            scene->handHover[i] = 0.0f;
        }
        scene->handAnimationCount = hand->count;
    }
    for (int i = 0; i < hand->count; ++i) {
        const bool isSelected = ((selected >> i) & 1u) != 0u;
        const float hoverTarget =
            ready && !scene->dragSelecting && scene->hoverCard == i ? 1.0f : 0.0f;
        const float liftTarget = (isSelected ? (float)GAME_LAYOUT_SELECTED_LIFT : 0.0f) +
                                 scene->handHover[i] * kHoverLift;

        scene->handHover[i] = Approach(scene->handHover[i], hoverTarget, 18.0f, dt);
        scene->handLift[i] = Approach(scene->handLift[i], liftTarget, 20.0f, dt);
    }
}

static void GameScene_UpdateMidgameMock(GameScene *scene)
{
    GameState *game = &scene->game;

    if (!GameScene_InteractionReady(scene) || !GameState_IsHumanTurn(game)) {
        return;
    }
    if (!scene->mockPlayed) {
        if (GameState_ApplyHint(game) && GameState_SelectedMask(game) != 0u) {
            GameState_PlaySelected(game);
        }
        scene->mockPlayed = true;
    } else if (!scene->mockHinted) {
        GameState_ApplyHint(game);
        scene->mockHinted = true;
    }
    scene->actionButtonsDirty = true;
}

static void GameScene_ConsumeEvents(GameScene *scene)
{
    GameState *game = &scene->game;
    const int eventCount = GameState_EventCount(game);

    for (int i = 0; i < eventCount; ++i) {
        const GameEvent *event = GameState_EventAt(game, i);

        if (event == NULL) {
            continue;
        }
        switch (event->type) {
        case GAME_EVENT_ROUND_STARTED:
            break;
        case GAME_EVENT_CARDS_PLAYED:
            App_PlaySound(scene->app, SOUND_PLAY_CARDS);
            scene->playAnimation = 1.0f;
            scene->lastAnimatedPlayer = event->player;
            scene->passTimers[PlayerIndex(event->player)] = 0.0f;
            break;
        case GAME_EVENT_PASSED:
            App_PlaySound(scene->app, SOUND_PASS);
            scene->passTimers[PlayerIndex(event->player)] = kPassChipSeconds;
            break;
        case GAME_EVENT_INVALID_MOVE:
            App_PlaySound(scene->app, SOUND_INVALID_MOVE);
            break;
        case GAME_EVENT_HINT:
            App_PlaySound(scene->app, SOUND_HINT);
            break;
        case GAME_EVENT_BOMB:
            App_PlaySound(scene->app, SOUND_BOMB);
            scene->bombAnimation = 1.0f;
            break;
        case GAME_EVENT_ROUND_ENDED:
            App_PlaySound(scene->app, SOUND_ROUND_END);
            App_PlaySound(scene->app, event->player == PLAYER_HUMAN ? SOUND_WIN : SOUND_LOSE);
            scene->roundResultPending = true;
            scene->roundResultDelay = 0.0f;
            break;
        case GAME_EVENT_TALK:
            if (event->player != PLAYER_HUMAN) {
                App_PlaySound(scene->app, SOUND_AI_TALK);
                App_PushOverlay(scene->app,
                                TalkBubbleOverlay_New(event->player, event->message));
            } else {
                App_PlaySound(scene->app, SOUND_TURN_PROMPT);
            }
            break;
        case GAME_EVENT_NONE:
            break;
        }
    }
    GameState_ClearEvents(game);
}

static void GameScene_ShowRoundResultOverlay(GameScene *scene)
{
    const RoundRecord *record;
    RoundRecord stored;

    if (!GameState_IsRoundOver(&scene->game) || scene->recordedRound) {
        return;
    }

    record = GameState_LastRoundRecord(&scene->game);
    stored = *record;
    if (!App_ViewerMode(scene->app)) {
        RoundRecorder_AppendToday(App_Recorder(scene->app), &stored);
    }
    for (int i = 0; i < kSeatCapacity; ++i) {
        scene->todayScores[i] += record->scores[i];
    }
    App_PushOverlay(scene->app, RoundResultOverlay_New(scene->app, &stored));
    scene->recordedRound = true;
    scene->roundResultPending = false;
}

static void GameScene_UpdateRoundResultDelay(GameScene *scene, float dt)
{
    if (!scene->roundResultPending || scene->recordedRound) {
        return;
    }

    /* Keep the final table visible briefly so the last played cards and revealed AI hands can be
     * read before the modal result screen covers them. */
    scene->roundResultDelay += dt;
    if (scene->roundResultDelay >= kRoundResultDelaySeconds) {
        GameScene_ShowRoundResultOverlay(scene);
    }
}

static void GameScene_OnEnter(void *user)
{
    GameScene *scene = (GameScene *)user;
    StatStore store;
    StatSummary summary;
    char today[PDK_DATE_KEY_CAP];

    if (!App_GameResourcesReady(scene->app)) {
        App_LoadGameResources(scene->app);
    }
    GameScene_InitializeExternalAi(scene);

    /* Today's aggregate score per seat, from the same per-round records the stats page reads. */
    StatStore_Init(&store, NULL);
    TodayDateKey(today, PDK_DATE_KEY_CAP);
    StatStore_SummarizeDay(&store, today, &summary);
    for (int i = 0; i < kSeatCapacity; ++i) {
        scene->todayScores[i] = summary.scores[i];
    }
    GameScene_StartNextRound(scene);
}

static void GameScene_Update(void *user, float dt)
{
    GameScene *scene = (GameScene *)user;
    GameState *game = &scene->game;
    bool hadEvents;
    bool ready;

    scene->time += dt;
    Button_Update(&scene->backButton, dt);
    ButtonGroup_UpdateAll(scene->buttons, scene->buttonCount, dt);
    for (int i = 0; i < kSeatCapacity; ++i) {
        scene->passTimers[i] = PDK_MAX(0.0f, scene->passTimers[i] - dt);
    }
    scene->toastAge += dt;
    if (strcmp(GameState_Toast(game), scene->toast) != 0) {
        Str_CopyTo(scene->toast, kToastCapacity, GameState_Toast(game));
        scene->toastAge = 0.0f;
    }

    if (!scene->handsSorted) {
        scene->dealElapsed += dt;
        while (scene->dealSoundCount < kDealSoundCount &&
               scene->dealElapsed >= (float)(scene->dealSoundCount + 1) * kDealCardInterval) {
            App_PlaySound(scene->app, SOUND_DEAL_CARD);
            scene->dealSoundCount++;
        }
        if (scene->dealElapsed >= (float)kDealSoundCount * kDealCardInterval + 0.1f) {
            for (int i = 0; i < kSeatCapacity; ++i) {
                scene->handsBeforeSort[i] = GameState_Players(game)[i].hand;
            }
            GameState_SortHands(game);
            scene->handsSorted = true;
            scene->sortAnimation = 1.0f;
            App_PlaySound(scene->app, SOUND_HINT);
            scene->actionButtonsDirty = true;
        }
    } else {
        scene->sortAnimation = PDK_MAX(0.0f, scene->sortAnimation - dt * kSortAnimationSpeed);
    }
    scene->playAnimation = PDK_MAX(0.0f, scene->playAnimation - dt * 2.6f);
    scene->bombAnimation = PDK_MAX(0.0f, scene->bombAnimation - dt * 0.9f);
    if (GameScene_InteractionReady(scene)) {
        GameState_Update(game, dt);
    }
    if (scene->midgameMock) {
        GameScene_UpdateMidgameMock(scene);
    }
    hadEvents = GameState_EventCount(game) > 0;
    GameScene_ConsumeEvents(scene);
    GameScene_UpdateRoundResultDelay(scene, dt);
    GameScene_UpdateHandAnimation(scene, dt);

    /* Button enablement can query rule search, so update it only after game or interaction state
     * changes instead of doing that work every frame/mouse move. */
    ready = GameScene_InteractionReady(scene);
    if (hadEvents || ready != scene->lastInteractionReady) {
        scene->actionButtonsDirty = true;
    }
    if (scene->actionButtonsDirty) {
        GameScene_UpdateActionButtons(scene);
    }
}

static void GameScene_Render(void *user, RenderContext *context)
{
    GameScene *scene = (GameScene *)user;
    float shakeX = 0.0f;
    float shakeY = 0.0f;

    if (!SpriteAtlas_Loaded(App_CardAtlas(scene->app))) {
        App_LoadGameResources(scene->app);
    }
    RenderContext_Clear(context, THEME_ROOM);
    {
        GradientStop stops[3];
        const Rect room = Rect_Make(0.0f, 0.0f, LogicalWidth, LogicalHeight);

        stops[0] = GradientStop_Make(0.0f, THEME_FELT_DEEP);
        stops[1] = GradientStop_Make(0.6f, THEME_ROOM);
        stops[2] = GradientStop_Make(1.0f, THEME_ROOM_DEEP);
        RenderContext_FillRectBrush(
            context, &room,
            RenderContext_Radial(context, Point_Make(640.0f, 330.0f), 900.0f, 620.0f, stops, 3));
    }

    if (scene->bombAnimation > 0.0f) {
        const float p = 1.0f - scene->bombAnimation;
        const float amplitude =
            9.0f * scene->bombAnimation * scene->bombAnimation * scene->bombAnimation;

        shakeX = sinf(p * 71.0f) * amplitude;
        shakeY = cosf(p * 53.0f) * amplitude * 0.6f;
    }
    RenderContext_PushTranslation(context, shakeX, shakeY);
    GameScene_DrawTable(context);
    Widgets_DrawVignette(context, 0.6f);
    GameScene_DrawAiSeat(scene, context, PLAYER_AI1);
    GameScene_DrawAiSeat(scene, context, PLAYER_AI2);
    GameScene_DrawTurnChip(scene, context);
    GameScene_DrawPlayedCards(scene, context);
    GameScene_DrawPassChips(scene, context);
    GameScene_DrawDealPile(scene, context);
    GameScene_DrawPlayerPlate(scene, context);
    ButtonGroup_DrawAll(context, scene->buttons, scene->buttonCount);
    GameScene_DrawToast(scene, context);
    GameScene_DrawPlayerHand(scene, context);
    RenderContext_PopTransform(context);

    GameScene_DrawBombEffect(scene, context);
    Button_Draw(&scene->backButton, context);
}

/* ---- input ----------------------------------------------------------- */

static bool GameScene_OnMouseMove(void *user, float x, float y)
{
    GameScene *scene = (GameScene *)user;
    int card;

    Button_UpdateHover(&scene->backButton, x, y);
    ButtonGroup_UpdateHover(scene->buttons, scene->buttonCount, x, y);
    card = GameScene_HitPlayerCard(scene, x, y);
    if (scene->dragSelecting && card >= 0 && GameScene_InteractionReady(scene)) {
        GameScene_DragPush(scene, card);
        if (card != scene->dragStartCard) {
            scene->dragMoved = true;
        }
    }
    scene->hoverCard = card;
    return true;
}

static bool GameScene_OnMouseDown(void *user, float x, float y)
{
    GameScene *scene = (GameScene *)user;
    int card;
    int hit;

    if (Button_HitTest(&scene->backButton, x, y)) {
        scene->backButton.pressT = 1.0f;
        App_PlaySound(scene->app, SOUND_BUTTON_CLICK);
        App_PushOverlay(scene->app, ReturnToMenuOverlay_New(scene->app));
        return true;
    }
    if (!GameScene_InteractionReady(scene)) {
        return true;
    }
    card = GameScene_HitPlayerCard(scene, x, y);
    if (card >= 0) {
        scene->dragSelecting = true;
        scene->dragMoved = false;
        scene->dragStartCard = card;
        scene->dragPathCount = 0;
        GameScene_DragPush(scene, card);
        scene->hoverCard = card;
        return true;
    }

    if (scene->actionButtonsDirty) {
        GameScene_UpdateActionButtons(scene);
    }
    hit = ButtonGroup_Hit(scene->buttons, scene->buttonCount, x, y);
    if (hit < 0) {
        return false;
    }
    if (hit == 0) {
        GameState_ToggleAutoplay(&scene->game);
        App_PlaySound(scene->app, SOUND_BUTTON_CLICK);
    } else if (hit == 1) {
        GameState_PassHuman(&scene->game);
    } else if (hit == 2) {
        GameState_ApplyHint(&scene->game);
    } else if (hit == 3) {
        GameState_PlaySelected(&scene->game);
    }
    GameScene_ConsumeEvents(scene);
    scene->actionButtonsDirty = true;
    GameScene_UpdateActionButtons(scene);
    return true;
}

static bool GameScene_OnMouseUp(void *user, float x, float y)
{
    GameScene *scene = (GameScene *)user;
    int card;

    if (!scene->dragSelecting) {
        return true;
    }
    card = GameScene_HitPlayerCard(scene, x, y);
    if (card >= 0) {
        GameScene_DragPush(scene, card);
        if (card != scene->dragStartCard) {
            scene->dragMoved = true;
        }
    }

    if (GameScene_InteractionReady(scene)) {
        if (scene->dragMoved && scene->dragPathCount > 1) {
            if (GameState_SelectBestPatternFromDraggedCards(&scene->game, scene->dragPath,
                                                            scene->dragPathCount)) {
                App_PlaySound(scene->app, SOUND_SELECT_CARD);
            } else {
                App_PlaySound(scene->app, SOUND_INVALID_MOVE);
            }
        } else if (scene->dragStartCard >= 0) {
            const bool wasSelected =
                ((GameState_SelectedMask(&scene->game) >> scene->dragStartCard) & 1u) != 0u;

            GameState_TogglePlayerCard(&scene->game, scene->dragStartCard);
            App_PlaySound(scene->app, wasSelected ? SOUND_DESELECT_CARD : SOUND_SELECT_CARD);
        }
        scene->actionButtonsDirty = true;
    }

    scene->dragSelecting = false;
    scene->dragMoved = false;
    scene->dragStartCard = -1;
    scene->dragPathCount = 0;
    if (scene->actionButtonsDirty) {
        GameScene_UpdateActionButtons(scene);
    }
    return true;
}

static bool GameScene_RestartRound(void *user)
{
    GameScene_StartNextRound((GameScene *)user);
    return true;
}

static void GameScene_Destroy(void *user)
{
    GameScene *scene = (GameScene *)user;

    if (scene == NULL) {
        return;
    }
    /* GameState owns the event queue and the audit trail, so the scene must release it. */
    GameState_Destroy(&scene->game);
    free(scene);
}

static const SceneVtbl kGameSceneVtbl = {
    .OnEnter = GameScene_OnEnter,
    .Update = GameScene_Update,
    .Render = GameScene_Render,
    .OnMouseMove = GameScene_OnMouseMove,
    .OnMouseDown = GameScene_OnMouseDown,
    .OnMouseUp = GameScene_OnMouseUp,
    .RestartRound = GameScene_RestartRound,
    .Destroy = GameScene_Destroy
};

Scene GameScene_New(void *app, bool mock, bool midgame)
{
    GameScene *scene = (GameScene *)malloc(sizeof(GameScene));
    Scene handle;

    if (scene == NULL) {
        handle.vtbl = NULL;
        handle.user = NULL;
        return handle;
    }
    /* Every member is explicitly initialised: the memset covers the fixed arrays and the
     * GameState (which GameState_Init then re-clears), and the defaults the C++ class carried as
     * member initialisers are written out below. */
    memset(scene, 0, sizeof(*scene));
    scene->app = app;
    scene->mock = mock;
    scene->midgameMock = midgame;
    GameState_Init(&scene->game);

    scene->backButton = Button_Make(Rect_Make(20.0f, 20.0f, 46.0f, 46.0f), "", UI_BUTTON_ICON);
    scene->backButton.icon = UI_ICON_BACK;
    scene->buttons[0] = Button_Make(Rect_Make(0.0f, 0.0f, 110.0f, 44.0f), "托管", UI_BUTTON_GHOST);
    scene->buttons[1] =
        Button_Make(Rect_Make(0.0f, 0.0f, 104.0f, 44.0f), "不要", UI_BUTTON_SECONDARY);
    scene->buttons[2] =
        Button_Make(Rect_Make(0.0f, 0.0f, 104.0f, 44.0f), "提示", UI_BUTTON_SECONDARY);
    scene->buttons[3] =
        Button_Make(Rect_Make(0.0f, 0.0f, 128.0f, 44.0f), "出牌", UI_BUTTON_PRIMARY);
    for (int i = 0; i < kButtonCapacity; ++i) {
        scene->buttons[i].fontSize = 18.0f;
    }
    scene->buttonCount = kButtonCapacity;
    scene->hoverCard = -1;
    scene->dragStartCard = -1;
    scene->actionButtonsDirty = true;
    scene->handsSorted = true;
    scene->lastAnimatedPlayer = PLAYER_HUMAN;
    GameScene_LayoutActionButtons(scene);

    handle.vtbl = &kGameSceneVtbl;
    handle.user = scene;
    return handle;
}
