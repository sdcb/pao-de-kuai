#include "audio/SoundCatalog.h"

#include "resources/ResourceIds.h"

namespace pdk::audio {

const std::vector<SoundCatalogEntry>& SoundCatalog() {
    static const std::vector<SoundCatalogEntry> catalog = {
        {SoundId::ButtonClick, IDR_WAV_UI_BUTTON_CLICK, "ui_button_click.wav", 0.55f},
        {SoundId::Confirm, IDR_WAV_UI_CONFIRM, "ui_confirm.wav", 0.60f},
        {SoundId::Cancel, IDR_WAV_UI_CANCEL, "ui_cancel.wav", 0.55f},
        {SoundId::Toast, IDR_WAV_UI_TOAST, "ui_toast.wav", 0.45f},
        {SoundId::Pause, IDR_WAV_UI_PAUSE, "ui_pause.wav", 0.50f},
        {SoundId::Resume, IDR_WAV_UI_RESUME, "ui_resume.wav", 0.50f},
        {SoundId::SelectCard, IDR_WAV_CARD_SELECT, "card_select.wav", 0.50f},
        {SoundId::DeselectCard, IDR_WAV_CARD_DESELECT, "card_deselect.wav", 0.48f},
        {SoundId::DealCard, IDR_WAV_CARD_DEAL, "card_deal.wav", 0.42f},
        {SoundId::PlayCards, IDR_WAV_CARD_PLAY, "card_play.wav", 0.62f},
        {SoundId::Pass, IDR_WAV_CARD_PASS, "card_pass.wav", 0.52f},
        {SoundId::Hint, IDR_WAV_CARD_HINT, "card_hint.wav", 0.50f},
        {SoundId::InvalidMove, IDR_WAV_GAME_INVALID_MOVE, "game_invalid_move.wav", 0.58f},
        {SoundId::TurnPrompt, IDR_WAV_GAME_TURN_PROMPT, "game_turn_prompt.wav", 0.52f},
        {SoundId::RoundStart, IDR_WAV_GAME_ROUND_START, "game_round_start.wav", 0.50f},
        {SoundId::RoundEnd, IDR_WAV_GAME_ROUND_END, "game_round_end.wav", 0.55f},
        {SoundId::Bomb, IDR_WAV_EVENT_BOMB, "event_bomb.wav", 0.75f},
        {SoundId::Spring, IDR_WAV_EVENT_SPRING, "event_spring.wav", 0.70f},
        {SoundId::Win, IDR_WAV_EVENT_WIN, "event_win.wav", 0.68f},
        {SoundId::Lose, IDR_WAV_EVENT_LOSE, "event_lose.wav", 0.58f},
        {SoundId::AiTalk, IDR_WAV_EVENT_AI_TALK, "event_ai_talk.wav", 0.42f}
    };
    return catalog;
}

} // namespace pdk::audio
