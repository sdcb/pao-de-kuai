#include "game/TurnRecord.h"

#include <cJSON.h>

#include <memory>

namespace pdk::game {
namespace {

struct JsonDeleter {
    void operator()(cJSON* value) const {
        cJSON_Delete(value);
    }
};

using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::string PrintJson(cJSON* json) {
    char* text = cJSON_PrintUnformatted(json);
    if (!text) {
        return {};
    }
    std::string result = text;
    cJSON_free(text);
    return result;
}

} // namespace

std::string PlayerLabel(rules::PlayerId player) {
    switch (player) {
    case rules::PlayerId::Player: return "player";
    case rules::PlayerId::Ai1: return "ai1";
    case rules::PlayerId::Ai2: return "ai2";
    }
    return "unknown";
}

std::string SourceLabel(TurnDecisionSource source) {
    switch (source) {
    case TurnDecisionSource::Human: return "human";
    case TurnDecisionSource::LocalAi: return "local_ai";
    case TurnDecisionSource::LlmAi: return "llm_ai";
    case TurnDecisionSource::System: return "system";
    }
    return "unknown";
}

std::string ReasonLabel(TurnDecisionReason reason) {
    switch (reason) {
    case TurnDecisionReason::NormalChoice: return "normal_choice";
    case TurnDecisionReason::CannotBeat: return "cannot_beat";
    case TurnDecisionReason::OnlyLegalMove: return "only_legal_move";
    case TurnDecisionReason::LlmFallback: return "llm_fallback";
    }
    return "unknown";
}

std::string ActionArgumentsJson(const GameAction& action) {
    JsonPtr root(cJSON_CreateObject());
    cJSON_AddStringToObject(root.get(), "action", action.action.c_str());
    cJSON* ranks = cJSON_CreateArray();
    for (const std::string& rank : action.ranks) {
        cJSON_AddItemToArray(ranks, cJSON_CreateString(rank.c_str()));
    }
    cJSON_AddItemToObject(root.get(), "ranks", ranks);
    if (!action.talk.empty()) {
        cJSON_AddStringToObject(root.get(), "talk", action.talk.c_str());
    }
    return PrintJson(root.get());
}

std::string ForcedMoveArgumentsJson(TurnDecisionReason reason, const GameAction& action) {
    JsonPtr root(cJSON_CreateObject());
    cJSON_AddStringToObject(root.get(), "reason", reason == TurnDecisionReason::CannotBeat ? "cannot_beat" : "only_legal_move");
    cJSON* ranks = cJSON_CreateArray();
    for (const std::string& rank : action.ranks) {
        cJSON_AddItemToArray(ranks, cJSON_CreateString(rank.c_str()));
    }
    cJSON_AddItemToObject(root.get(), "ranks", ranks);
    return PrintJson(root.get());
}

} // namespace pdk::game
