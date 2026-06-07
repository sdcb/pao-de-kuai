#include "stats/StatStore.h"

#include <cJSON.h>

#include <chrono>
#include <fstream>
#include <iomanip>
#include <sstream>

namespace pdk::stats {
namespace {

std::string ReadFile(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

std::string LocalDateTime(const char* fmt) {
    const auto now = std::chrono::system_clock::now();
    const std::time_t time = std::chrono::system_clock::to_time_t(now);
    std::tm local{};
#if defined(_WIN32)
    localtime_s(&local, &time);
#else
    localtime_r(&time, &local);
#endif
    std::ostringstream out;
    out << std::put_time(&local, fmt);
    return out.str();
}

cJSON* ScoresToJson(const std::array<int, 3>& scores) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddNumberToObject(object, "player", scores[0]);
    cJSON_AddNumberToObject(object, "ai1", scores[1]);
    cJSON_AddNumberToObject(object, "ai2", scores[2]);
    return object;
}

std::array<int, 3> ScoresFromJson(const cJSON* object) {
    std::array<int, 3> scores{0, 0, 0};
    if (!cJSON_IsObject(object)) {
        return scores;
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, "player"); cJSON_IsNumber(value)) {
        scores[0] = value->valueint;
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, "ai1"); cJSON_IsNumber(value)) {
        scores[1] = value->valueint;
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, "ai2"); cJSON_IsNumber(value)) {
        scores[2] = value->valueint;
    }
    return scores;
}

rules::PlayerId PlayerFromKey(const char* key) {
    if (!key) {
        return rules::PlayerId::Player;
    }
    const std::string value = key;
    if (value == "ai1") {
        return rules::PlayerId::Ai1;
    }
    if (value == "ai2") {
        return rules::PlayerId::Ai2;
    }
    return rules::PlayerId::Player;
}

cJSON* RoundToJson(const RoundRecord& round) {
    cJSON* object = cJSON_CreateObject();
    cJSON_AddStringToObject(object, "startedAt", round.startedAt.c_str());
    cJSON_AddStringToObject(object, "endedAt", round.endedAt.c_str());
    cJSON_AddStringToObject(object, "winner", rules::PlayerKey(round.winner).c_str());
    cJSON_AddStringToObject(object, "playerName", round.playerName.c_str());
    cJSON_AddItemToObject(object, "scores", ScoresToJson(round.scores));
    cJSON_AddItemToObject(object, "remainingCards", ScoresToJson(round.remainingCards));

    cJSON* bombs = cJSON_CreateArray();
    for (const rules::BombScoreEvent& bomb : round.bombs) {
        cJSON* item = cJSON_CreateObject();
        cJSON_AddStringToObject(item, "by", rules::PlayerKey(bomb.by).c_str());
        cJSON_AddNumberToObject(item, "score", bomb.score);
        cJSON_AddItemToArray(bombs, item);
    }
    cJSON_AddItemToObject(object, "bombs", bombs);

    cJSON* spring = cJSON_CreateObject();
    cJSON_AddBoolToObject(spring, "enabled", round.spring.enabled);
    cJSON* losers = cJSON_CreateArray();
    for (rules::PlayerId loser : round.spring.losers) {
        cJSON_AddItemToArray(losers, cJSON_CreateString(rules::PlayerKey(loser).c_str()));
    }
    cJSON_AddItemToObject(spring, "losers", losers);
    cJSON_AddItemToObject(object, "spring", spring);
    return object;
}

RoundRecord RoundFromJson(const cJSON* object) {
    RoundRecord round;
    if (!cJSON_IsObject(object)) {
        return round;
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, "startedAt"); cJSON_IsString(value)) {
        round.startedAt = value->valuestring;
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, "endedAt"); cJSON_IsString(value)) {
        round.endedAt = value->valuestring;
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, "winner"); cJSON_IsString(value)) {
        round.winner = PlayerFromKey(value->valuestring);
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, "playerName"); cJSON_IsString(value)) {
        round.playerName = value->valuestring;
    }
    round.scores = ScoresFromJson(cJSON_GetObjectItemCaseSensitive(object, "scores"));
    round.remainingCards = ScoresFromJson(cJSON_GetObjectItemCaseSensitive(object, "remainingCards"));

    const cJSON* bombs = cJSON_GetObjectItemCaseSensitive(object, "bombs");
    if (cJSON_IsArray(bombs)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, bombs) {
            rules::BombScoreEvent bomb;
            if (const cJSON* by = cJSON_GetObjectItemCaseSensitive(item, "by"); cJSON_IsString(by)) {
                bomb.by = PlayerFromKey(by->valuestring);
            }
            if (const cJSON* score = cJSON_GetObjectItemCaseSensitive(item, "score"); cJSON_IsNumber(score)) {
                bomb.score = score->valueint;
            }
            round.bombs.push_back(bomb);
        }
    }

    const cJSON* spring = cJSON_GetObjectItemCaseSensitive(object, "spring");
    if (cJSON_IsObject(spring)) {
        if (const cJSON* enabled = cJSON_GetObjectItemCaseSensitive(spring, "enabled"); cJSON_IsBool(enabled)) {
            round.spring.enabled = cJSON_IsTrue(enabled);
        }
        const cJSON* losers = cJSON_GetObjectItemCaseSensitive(spring, "losers");
        if (cJSON_IsArray(losers)) {
            const cJSON* item = nullptr;
            cJSON_ArrayForEach(item, losers) {
                if (cJSON_IsString(item)) {
                    round.spring.losers.push_back(PlayerFromKey(item->valuestring));
                }
            }
        }
    }
    return round;
}

void Accumulate(StatSummary& summary, const DailyStat& day) {
    for (const RoundRecord& round : day.rounds) {
        summary.rounds++;
        for (int i = 0; i < 3; ++i) {
            summary.scores[i] += round.scores[i];
        }
        summary.bombs += static_cast<int>(round.bombs.size());
        summary.springLosers += static_cast<int>(round.spring.losers.size());
        if (round.scores[0] > summary.bestSingleRoundPlayerScore) {
            summary.bestSingleRoundPlayerScore = round.scores[0];
        }
    }
}

} // namespace

std::string TodayDateKey() {
    return LocalDateTime("%Y%m%d");
}

std::string NowTimeText() {
    return LocalDateTime("%H:%M:%S");
}

StatStore::StatStore(std::filesystem::path root) : root_(std::move(root)) {}

std::filesystem::path StatStore::DayPath(const std::string& date) const {
    return root_ / "stat" / (date + ".json");
}

DailyStat StatStore::LoadDay(const std::string& date) const {
    DailyStat day;
    day.date = date;

    const std::string content = ReadFile(DayPath(date));
    if (content.empty()) {
        return day;
    }

    cJSON* root = cJSON_Parse(content.c_str());
    if (!root) {
        return day;
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "date"); cJSON_IsString(value)) {
        day.date = value->valuestring;
    }
    const cJSON* rounds = cJSON_GetObjectItemCaseSensitive(root, "rounds");
    if (cJSON_IsArray(rounds)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, rounds) {
            day.rounds.push_back(RoundFromJson(item));
        }
    }
    cJSON_Delete(root);
    return day;
}

bool StatStore::SaveDay(const DailyStat& day) const {
    std::filesystem::create_directories(root_ / "stat");
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "date", day.date.c_str());
    cJSON* rounds = cJSON_CreateArray();
    for (const RoundRecord& round : day.rounds) {
        cJSON_AddItemToArray(rounds, RoundToJson(round));
    }
    cJSON_AddItemToObject(root, "rounds", rounds);

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) {
        return false;
    }

    std::ofstream out(DayPath(day.date), std::ios::binary | std::ios::trunc);
    if (!out) {
        cJSON_free(text);
        return false;
    }
    out << text;
    cJSON_free(text);
    return true;
}

bool StatStore::AppendRound(const std::string& date, const RoundRecord& round) const {
    DailyStat day = LoadDay(date);
    day.date = date;
    day.rounds.push_back(round);
    return SaveDay(day);
}

StatSummary StatStore::SummarizeDay(const std::string& date) const {
    StatSummary summary;
    Accumulate(summary, LoadDay(date));
    return summary;
}

StatSummary StatStore::SummarizeMonth(const std::string& yyyymm) const {
    StatSummary summary;
    const std::filesystem::path statDir = root_ / "stat";
    if (!std::filesystem::exists(statDir)) {
        return summary;
    }
    for (const auto& entry : std::filesystem::directory_iterator(statDir)) {
        const std::string name = entry.path().stem().string();
        if (entry.is_regular_file() && name.rfind(yyyymm, 0) == 0) {
            Accumulate(summary, LoadDay(name));
        }
    }
    return summary;
}

StatSummary StatStore::SummarizeHistory() const {
    StatSummary summary;
    const std::filesystem::path statDir = root_ / "stat";
    if (!std::filesystem::exists(statDir)) {
        return summary;
    }
    for (const auto& entry : std::filesystem::directory_iterator(statDir)) {
        if (entry.is_regular_file() && entry.path().extension() == ".json") {
            Accumulate(summary, LoadDay(entry.path().stem().string()));
        }
    }
    return summary;
}

} // namespace pdk::stats
