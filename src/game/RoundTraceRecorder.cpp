#include "game/RoundTraceRecorder.h"

#include "core/CppCompat.h"
#include "stats/CppCompat.h"

#include <cJSON.h>

#include <algorithm>
#include <utility>

#ifndef PDK_APP_VERSION
#define PDK_APP_VERSION "unknown"
#endif

#ifndef PDK_BUILD_REVISION
#define PDK_BUILD_REVISION "unknown"
#endif

namespace pdk::game {
namespace {

cJSON* CardsToJson(const rules::Cards& cards) {
    cJSON* array = cJSON_CreateArray();
    for (rules::Card card : cards) {
        cJSON_AddItemToArray(array, cJSON_CreateString(rules::ToString(card).c_str()));
    }
    return array;
}

cJSON* PatternToJson(const std::optional<rules::HandPattern>& pattern) {
    if (!pattern) {
        return cJSON_CreateNull();
    }

    cJSON* object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "type", rules::PatternName(pattern->type).c_str());
    cJSON_AddStringToObject(object, "mainRank", rules::RankName(pattern->mainRank).c_str());
    cJSON_AddNumberToObject(object, "cardCount", pattern->cardCount);
    cJSON_AddNumberToObject(object, "groupCount", pattern->groupCount);
    cJSON_AddBoolToObject(object, "lastHandShort", pattern->lastHandShort);
    cJSON_AddStringToObject(object, "description", rules::PatternDescription(*pattern).c_str());
    return object;
}

cJSON* ActionToJson(const GameAction& action) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "action", action.action.c_str());
    cJSON* ranks = cJSON_CreateArray();
    for (const std::string& rank : action.ranks) {
        cJSON_AddItemToArray(ranks, cJSON_CreateString(rank.c_str()));
    }
    cJSON_AddItemToObject(object, "ranks", ranks);
    return object;
}

cJSON* HandsToJson(const std::array<rules::Cards, 3>& hands) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddItemToObject(object, "player", CardsToJson(hands[0]));
    cJSON_AddItemToObject(object, "ai1", CardsToJson(hands[1]));
    cJSON_AddItemToObject(object, "ai2", CardsToJson(hands[2]));
    return object;
}

cJSON* SnapshotToJson(const TurnSnapshot& snapshot) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddItemToObject(object, "hands", HandsToJson(snapshot.hands));
    cJSON_AddItemToObject(object, "lastCards", CardsToJson(snapshot.lastCards));
    cJSON_AddItemToObject(object, "lastPattern", PatternToJson(snapshot.lastPattern));
    cJSON_AddStringToObject(object, "lastMovePlayer", rules::PlayerKey(snapshot.lastMovePlayer).c_str());
    cJSON_AddStringToObject(object, "currentPlayer", rules::PlayerKey(snapshot.currentPlayer).c_str());
    cJSON_AddNumberToObject(object, "passCount", snapshot.passCount);
    cJSON* remaining = cJSON_CreateObject();
    cJSON_AddNumberToObject(remaining, "player", static_cast<int>(snapshot.hands[0].size()));
    cJSON_AddNumberToObject(remaining, "ai1", static_cast<int>(snapshot.hands[1].size()));
    cJSON_AddNumberToObject(remaining, "ai2", static_cast<int>(snapshot.hands[2].size()));
    cJSON_AddItemToObject(object, "remainingCards", remaining);
    return object;
}

cJSON* TraceMetaToJson(const TurnDecisionTrace& trace) {
    cJSON* object = cJSON_CreateObject();
    if (!trace.reasoningContent.empty()) {
        cJSON_AddStringToObject(object, "reasoningContent", trace.reasoningContent.c_str());
    }
    if (!trace.errorMessage.empty()) {
        cJSON_AddStringToObject(object, "errorMessage", trace.errorMessage.c_str());
    }
    return object;
}

void AddStrategyFields(cJSON* object, const StrategyMetadata& strategy) {
    cJSON_AddStringToObject(object, "strategy", strategy.strategy.c_str());
    cJSON_AddStringToObject(object, "strategyVersion", strategy.strategyVersion.c_str());
}

cJSON* TurnToJson(const TurnRecord& record) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddNumberToObject(object, "turnNo", record.turnNo);
    cJSON_AddStringToObject(object, "actor", rules::PlayerKey(record.actor).c_str());
    cJSON_AddStringToObject(object, "source", SourceLabel(record.source).c_str());
    cJSON_AddStringToObject(object, "reason", ReasonLabel(record.reason).c_str());
    cJSON_AddBoolToObject(object, "accepted", record.accepted);
    cJSON_AddStringToObject(object, "validationMessage", record.validationMessage.c_str());
    AddStrategyFields(object, record.strategy);
    cJSON_AddItemToObject(object, "before", SnapshotToJson(record.before));
    cJSON_AddItemToObject(object, "after", SnapshotToJson(record.after));
    cJSON_AddItemToObject(object, "requestedAction", ActionToJson(record.requestedAction));
    cJSON_AddItemToObject(object, "finalAction", ActionToJson(record.finalAction));
    cJSON_AddItemToObject(object, "finalCards", CardsToJson(record.finalCards));
    cJSON_AddItemToObject(object, "finalPattern", PatternToJson(record.finalPattern));
    cJSON_AddItemToObject(object, "trace", TraceMetaToJson(record.trace));
    return object;
}

cJSON* PlayersToJson(
    const std::array<PlayerState, 3>& players,
    const std::array<StrategyMetadata, 3>& strategies) {
    cJSON* array = cJSON_CreateArray();
    for (int i = 0; i < 3; ++i) {
        cJSON* object = cJSON_CreateObject();
        const rules::PlayerId id = rules::PlayerFromIndex(i);
        cJSON_AddStringToObject(object, "id", rules::PlayerKey(id).c_str());
        cJSON_AddStringToObject(object, "name", players[static_cast<std::size_t>(i)].name.c_str());
        cJSON_AddStringToObject(object, "kind", id == PLAYER_HUMAN ? "human" : "ai");
        AddStrategyFields(object, strategies[static_cast<std::size_t>(i)]);
        cJSON_AddItemToObject(object, "initialHand", CardsToJson(players[static_cast<std::size_t>(i)].hand));
        cJSON_AddItemToArray(array, object);
    }
    return array;
}

cJSON* ScoresToJson(const std::array<int, 3>& values) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddNumberToObject(object, "player", values[0]);
    cJSON_AddNumberToObject(object, "ai1", values[1]);
    cJSON_AddNumberToObject(object, "ai2", values[2]);
    return object;
}

cJSON* ResultToJson(const stats::RoundRecord& result) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "startedAt", result.startedAt.c_str());
    cJSON_AddStringToObject(object, "endedAt", result.endedAt.c_str());
    cJSON_AddStringToObject(object, "winner", rules::PlayerKey(result.winner).c_str());
    cJSON_AddItemToObject(object, "scores", ScoresToJson(result.scores));
    cJSON_AddItemToObject(object, "remainingCards", ScoresToJson(result.remainingCards));

    cJSON* bombs = cJSON_CreateArray();
    for (const rules::BombScoreEvent& bomb : result.bombs) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "by", rules::PlayerKey(bomb.by).c_str());
        cJSON_AddNumberToObject(item, "score", bomb.score);
        cJSON_AddBoolToObject(item, "beaten", bomb.beaten);
        cJSON_AddItemToArray(bombs, item);
    }
    cJSON_AddItemToObject(object, "bombs", bombs);

    cJSON* spring = cJSON_CreateObject();
    cJSON_AddBoolToObject(spring, "enabled", result.spring.enabled);
    cJSON* losers = cJSON_CreateArray();
    for (rules::PlayerId loser : result.spring.losers) {
        cJSON_AddItemToArray(losers, cJSON_CreateString(rules::PlayerKey(loser).c_str()));
    }
    cJSON_AddItemToObject(spring, "losers", losers);
    cJSON_AddItemToObject(object, "spring", spring);
    return object;
}

std::string TimeKey(std::string value) {
    value.erase(std::remove(value.begin(), value.end(), ':'), value.end());
    return value.empty() ? "000000" : value;
}

std::string TracePath(const std::string& root, const RoundTrace& trace) {
    const std::string date = stats::TodayDateKey();
    std::string name = TimeKey(trace.result.endedAt);
    name += "-seed";
    core::AppendNumber(name, trace.seed);
    name += "-turns";
    core::AppendNumber(name, trace.turns.size());
    name += ".json";
    return core::JoinPath(core::JoinPath(core::JoinPath(root, "round-traces"), date), name);
}

} // namespace

RoundTraceRecorder::RoundTraceRecorder(std::string root)
    : root_(root.empty() ? core::CurrentDirectory() : std::move(root)) {}

bool RoundTraceRecorder::WriteRound(const RoundTrace& trace, std::string* writtenPath) const {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "schemaVersion", 2);
    cJSON_AddStringToObject(root, "recordType", "pdk_round_trace");
    cJSON_AddStringToObject(root, "appVersion", PDK_APP_VERSION);
    cJSON_AddStringToObject(root, "buildRevision", PDK_BUILD_REVISION);
    cJSON_AddStringToObject(root, "rulesVersion", "pdk48-v1");
    cJSON_AddStringToObject(root, "turnOrder", "counterclockwise");
    cJSON_AddStringToObject(root, "date", stats::TodayDateKey().c_str());
    cJSON_AddNumberToObject(root, "seed", trace.seed);
    cJSON_AddStringToObject(root, "playerName", trace.playerName.c_str());
    cJSON_AddStringToObject(root, "startedAt", trace.startedAt.c_str());
    cJSON_AddStringToObject(root, "roundLeader", rules::PlayerKey(trace.roundLeader).c_str());
    cJSON_AddItemToObject(root, "players", PlayersToJson(trace.initialPlayers, trace.strategies));
    cJSON_AddItemToObject(root, "initialHands", HandsToJson({
        trace.initialPlayers[0].hand,
        trace.initialPlayers[1].hand,
        trace.initialPlayers[2].hand
    }));

    cJSON* turns = cJSON_CreateArray();
    for (const TurnRecord& turn : trace.turns) {
        cJSON_AddItemToArray(turns, TurnToJson(turn));
    }
    cJSON_AddItemToObject(root, "turns", turns);
    cJSON_AddItemToObject(root, "result", ResultToJson(trace.result));

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) {
        return false;
    }

    const std::string path = TracePath(root_, trace);
    const bool ok = core::WriteTextFile(path, text);
    cJSON_free(text);
    if (ok && writtenPath) {
        *writtenPath = path;
    }
    return ok;
}

} // namespace pdk::game
