#include <doctest/doctest.h>

#include "rules/Scoring.h"
#include "stats/AppSettings.h"
#include "stats/StatStore.h"

#include <filesystem>
#include <fstream>

using namespace pdk;

TEST_CASE("settings and daily stats use current working directory style json") {
    const auto root = std::filesystem::temp_directory_path() / "pao_de_kuai_rules_tests";
    std::filesystem::remove_all(root);
    std::filesystem::create_directories(root);

    stats::AppSettings settings;
    settings.playerName = "Tester";
    settings.masterVolume = 0.5f;
    settings.roundTraceEnabled = true;
    const auto settingsPath = (root / "appsettings.json").string();
    CHECK(stats::SaveAppSettings(settings, settingsPath));
    const stats::AppSettings loaded = stats::LoadAppSettings(settingsPath);
    CHECK(loaded.playerName == "Tester");
    CHECK(loaded.masterVolume == doctest::Approx(0.5));
    CHECK(loaded.roundTraceEnabled);
    CHECK(loaded.ai1 == "basic");
    CHECK(loaded.ai2 == "basic");
    std::ifstream settingsFile(settingsPath, std::ios::binary);
    const std::string settingsJson((std::istreambuf_iterator<char>(settingsFile)), std::istreambuf_iterator<char>());
    settingsFile.close();
    CHECK(settingsJson.find("cardScale") == std::string::npos);
    CHECK(settingsJson.find("animationSpeed") == std::string::npos);
    CHECK(settingsJson.find("roundTraceEnabled") != std::string::npos);

    stats::StatStore store(root.string());
    stats::RoundRecord round;
    round.startedAt = "20:12:31";
    round.endedAt = "20:16:02";
    round.winner = rules::PlayerId::Player;
    round.playerName = "Tester";
    round.scores = {18, -8, -10};
    round.remainingCards = {0, 8, 10};
    round.bombs = {rules::BombScoreEvent{rules::PlayerId::Player, 20}};
    CHECK(store.AppendRound("20260606", round));

    const stats::StatSummary day = store.SummarizeDay("20260606");
    CHECK(day.rounds == 1);
    CHECK(day.scores[0] == 18);
    CHECK(day.bombs == 1);
    const stats::StatSummary month = store.SummarizeMonth("202606");
    CHECK(month.rounds == 1);
    const stats::StatSummary history = store.SummarizeHistory();
    CHECK(history.rounds == 1);

    std::filesystem::remove_all(root);
}
