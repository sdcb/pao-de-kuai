#include "audio/SoundCatalog.h"

#include "resources/ResourceIds.h"

/*
 * Indexed by SoundId, so entry `i` is the sound `i`; a static assertion below
 * catches the table getting out of step with the enum.
 */

static const SoundCatalogEntry kSoundCatalog[SOUND_COUNT] = {
    {SOUND_BUTTON_CLICK, IDR_MP3_UI_BUTTON_CLICK, "ui_button_click.mp3", 0.55f},
    {SOUND_CONFIRM, IDR_MP3_UI_CONFIRM, "ui_confirm.mp3", 0.60f},
    {SOUND_CANCEL, IDR_MP3_UI_CANCEL, "ui_cancel.mp3", 0.55f},
    {SOUND_TOAST, IDR_MP3_UI_TOAST, "ui_toast.mp3", 0.45f},
    {SOUND_PAUSE, IDR_MP3_UI_PAUSE, "ui_pause.mp3", 0.50f},
    {SOUND_RESUME, IDR_MP3_UI_RESUME, "ui_resume.mp3", 0.50f},
    {SOUND_SELECT_CARD, IDR_MP3_CARD_SELECT, "card_select.mp3", 0.50f},
    {SOUND_DESELECT_CARD, IDR_MP3_CARD_DESELECT, "card_deselect.mp3", 0.48f},
    {SOUND_DEAL_CARD, IDR_MP3_CARD_DEAL, "card_deal.mp3", 0.42f},
    {SOUND_PLAY_CARDS, IDR_MP3_CARD_PLAY, "card_play.mp3", 0.62f},
    {SOUND_PASS, IDR_MP3_CARD_PASS, "card_pass.mp3", 0.52f},
    {SOUND_HINT, IDR_MP3_CARD_HINT, "card_hint.mp3", 0.50f},
    {SOUND_INVALID_MOVE, IDR_MP3_GAME_INVALID_MOVE, "game_invalid_move.mp3", 0.58f},
    {SOUND_TURN_PROMPT, IDR_MP3_GAME_TURN_PROMPT, "game_turn_prompt.mp3", 0.52f},
    {SOUND_ROUND_START, IDR_MP3_GAME_ROUND_START, "game_round_start.mp3", 0.50f},
    {SOUND_ROUND_END, IDR_MP3_GAME_ROUND_END, "game_round_end.mp3", 0.55f},
    {SOUND_BOMB, IDR_MP3_EVENT_BOMB, "event_bomb.mp3", 0.75f},
    {SOUND_SPRING, IDR_MP3_EVENT_SPRING, "event_spring.mp3", 0.70f},
    {SOUND_WIN, IDR_MP3_EVENT_WIN, "event_win.mp3", 0.68f},
    {SOUND_LOSE, IDR_MP3_EVENT_LOSE, "event_lose.mp3", 0.58f},
    {SOUND_AI_TALK, IDR_MP3_EVENT_AI_TALK, "event_ai_talk.mp3", 0.42f}
};

_Static_assert(sizeof(kSoundCatalog) / sizeof(kSoundCatalog[0]) == SOUND_COUNT,
               "sound catalog must have one entry per SoundId");

const SoundCatalogEntry *SoundCatalog_Entries(int *count)
{
    if (count != NULL) {
        *count = SOUND_COUNT;
    }
    return kSoundCatalog;
}
