#include "audio/SoundCatalog.h"

#include "resources/ResourceIds.h"

namespace pdk::audio {

const std::vector<SoundCatalogEntry>& SoundCatalog() {
    static const std::vector<SoundCatalogEntry> catalog = {
        {SoundId::ButtonClick, IDR_MP3_UI_BUTTON_CLICK, "ui_button_click.mp3", 0.55f},
        {SoundId::Confirm, IDR_MP3_UI_CONFIRM, "ui_confirm.mp3", 0.60f},
        {SoundId::Cancel, IDR_MP3_UI_CANCEL, "ui_cancel.mp3", 0.55f},
        {SoundId::Toast, IDR_MP3_UI_TOAST, "ui_toast.mp3", 0.45f},
        {SoundId::Pause, IDR_MP3_UI_PAUSE, "ui_pause.mp3", 0.50f},
        {SoundId::Resume, IDR_MP3_UI_RESUME, "ui_resume.mp3", 0.50f},
        {SoundId::SelectCard, IDR_MP3_CARD_SELECT, "card_select.mp3", 0.50f},
        {SoundId::DeselectCard, IDR_MP3_CARD_DESELECT, "card_deselect.mp3", 0.48f},
        {SoundId::DealCard, IDR_MP3_CARD_DEAL, "card_deal.mp3", 0.42f},
        {SoundId::PlayCards, IDR_MP3_CARD_PLAY, "card_play.mp3", 0.62f},
        {SoundId::Pass, IDR_MP3_CARD_PASS, "card_pass.mp3", 0.52f},
        {SoundId::Hint, IDR_MP3_CARD_HINT, "card_hint.mp3", 0.50f},
        {SoundId::InvalidMove, IDR_MP3_GAME_INVALID_MOVE, "game_invalid_move.mp3", 0.58f},
        {SoundId::TurnPrompt, IDR_MP3_GAME_TURN_PROMPT, "game_turn_prompt.mp3", 0.52f},
        {SoundId::RoundStart, IDR_MP3_GAME_ROUND_START, "game_round_start.mp3", 0.50f},
        {SoundId::RoundEnd, IDR_MP3_GAME_ROUND_END, "game_round_end.mp3", 0.55f},
        {SoundId::Bomb, IDR_MP3_EVENT_BOMB, "event_bomb.mp3", 0.75f},
        {SoundId::Spring, IDR_MP3_EVENT_SPRING, "event_spring.mp3", 0.70f},
        {SoundId::Win, IDR_MP3_EVENT_WIN, "event_win.mp3", 0.68f},
        {SoundId::Lose, IDR_MP3_EVENT_LOSE, "event_lose.mp3", 0.58f},
        {SoundId::AiTalk, IDR_MP3_EVENT_AI_TALK, "event_ai_talk.mp3", 0.42f}
    };
    return catalog;
}

} // namespace pdk::audio
