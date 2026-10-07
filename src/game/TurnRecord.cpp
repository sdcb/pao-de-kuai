#include "game/TurnRecord.h"

namespace pdk::game {

std::string PlayerLabel(rules::PlayerId player) {
    switch (player) {
    case PLAYER_HUMAN: return "player";
    case PLAYER_AI1: return "ai1";
    case PLAYER_AI2: return "ai2";
    }
    return "unknown";
}

std::string SourceLabel(TurnDecisionSource source) {
    switch (source) {
    case TurnDecisionSource::Human: return "human";
    case TurnDecisionSource::LocalAi: return "local_ai";
    case TurnDecisionSource::System: return "system";
    }
    return "unknown";
}

std::string ReasonLabel(TurnDecisionReason reason) {
    switch (reason) {
    case TurnDecisionReason::NormalChoice: return "normal_choice";
    case TurnDecisionReason::CannotBeat: return "cannot_beat";
    case TurnDecisionReason::OnlyLegalMove: return "only_legal_move";
    }
    return "unknown";
}

} // namespace pdk::game
