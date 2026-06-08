#include "ai/TurnRecord.h"

#include <cJSON.h>

#include <memory>

namespace pdk::ai {
namespace {

struct JsonDeleter {
    void operator()(cJSON* value) const {
        cJSON_Delete(value);
    }
};

using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::string PrintJson(cJSON* json) {
    char* text = cJSON_Print(json);
    if (!text) {
        return {};
    }
    std::string result = text;
    cJSON_free(text);
    return result;
}

cJSON* CardsJson(const rules::Cards& cards) {
    cJSON* array = cJSON_CreateArray();
    for (rules::Card card : cards) {
        cJSON_AddItemToArray(array, cJSON_CreateString(rules::ToString(card).c_str()));
    }
    return array;
}

cJSON* RanksJson(const std::vector<std::string>& ranks) {
    cJSON* array = cJSON_CreateArray();
    for (const std::string& rank : ranks) {
        cJSON_AddItemToArray(array, cJSON_CreateString(rank.c_str()));
    }
    return array;
}

cJSON* PatternJson(const std::optional<rules::HandPattern>& pattern) {
    if (!pattern) {
        return cJSON_CreateNull();
    }
    cJSON* object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "type", rules::PatternName(pattern->type).c_str());
    cJSON_AddStringToObject(object, "mainRank", rules::RankName(pattern->mainRank).c_str());
    cJSON_AddNumberToObject(object, "cardCount", pattern->cardCount);
    cJSON_AddNumberToObject(object, "groupCount", pattern->groupCount);
    cJSON_AddBoolToObject(object, "lastHandShort", pattern->lastHandShort);
    return object;
}

cJSON* SnapshotJson(const TurnSnapshot& snapshot) {
    cJSON* object = cJSON_CreateObject();
    cJSON* hands = cJSON_CreateObject();
    for (int i = 0; i < 3; ++i) {
        cJSON_AddItemToObject(hands, PlayerLabel(rules::PlayerFromIndex(i)).c_str(), CardsJson(snapshot.hands[static_cast<std::size_t>(i)]));
    }
    cJSON_AddItemToObject(object, "hands", hands);
    cJSON_AddItemToObject(object, "lastCards", CardsJson(snapshot.lastCards));
    cJSON_AddItemToObject(object, "lastPattern", PatternJson(snapshot.lastPattern));
    cJSON_AddStringToObject(object, "lastMovePlayer", PlayerLabel(snapshot.lastMovePlayer).c_str());
    cJSON_AddStringToObject(object, "currentPlayer", PlayerLabel(snapshot.currentPlayer).c_str());
    cJSON_AddNumberToObject(object, "passCount", snapshot.passCount);
    return object;
}

cJSON* MoveJson(const PdkAiMove& move) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "action", move.action.c_str());
    cJSON_AddItemToObject(object, "ranks", RanksJson(move.ranks));
    if (!move.talk.empty()) {
        cJSON_AddStringToObject(object, "talk", move.talk.c_str());
    }
    return object;
}

std::string RelativePathText(const std::filesystem::path& path) {
    return path.generic_string();
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

std::string ToJson(const std::vector<TurnRecord>& records) {
    JsonPtr root(cJSON_CreateArray());
    for (const TurnRecord& record : records) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddNumberToObject(item, "turnNo", record.turnNo);
        cJSON_AddStringToObject(item, "actor", PlayerLabel(record.actor).c_str());
        cJSON_AddStringToObject(item, "source", SourceLabel(record.source).c_str());
        cJSON_AddStringToObject(item, "reason", ReasonLabel(record.reason).c_str());
        cJSON_AddItemToObject(item, "before", SnapshotJson(record.before));
        cJSON_AddItemToObject(item, "after", SnapshotJson(record.after));
        cJSON_AddItemToObject(item, "requestedAction", MoveJson(record.requestedAction));
        cJSON_AddItemToObject(item, "finalAction", MoveJson(record.finalAction));
        cJSON_AddItemToObject(item, "finalCards", CardsJson(record.finalCards));
        cJSON_AddItemToObject(item, "finalPattern", PatternJson(record.finalPattern));
        cJSON_AddBoolToObject(item, "accepted", record.accepted);
        cJSON_AddStringToObject(item, "validationMessage", record.validationMessage.c_str());
        cJSON* trace = cJSON_CreateObject();
        cJSON_AddStringToObject(trace, "reasoning_content", record.trace.reasoningContent.c_str());
        if (!record.trace.requestLogPath.empty()) {
            cJSON_AddStringToObject(trace, "requestLogPath", RelativePathText(record.trace.requestLogPath).c_str());
        }
        if (!record.trace.responseLogPath.empty()) {
            cJSON_AddStringToObject(trace, "responseLogPath", RelativePathText(record.trace.responseLogPath).c_str());
        }
        cJSON_AddItemToObject(item, "trace", trace);
        cJSON_AddItemToArray(root.get(), item);
    }
    return PrintJson(root.get());
}

} // namespace pdk::ai
