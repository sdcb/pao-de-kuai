#pragma once

#include <string>

namespace pdk::stats {

struct AppSettings {
    std::string playerName{"\xE6\x9D\x8E\xE5\xA7\x90"};
    float masterVolume{0.8f};
    int windowWidth{1280};
    int windowHeight{720};
};

AppSettings LoadAppSettings(const std::string& path = "appsettings.json");
bool SaveAppSettings(const AppSettings& settings, const std::string& path = "appsettings.json");

} // namespace pdk::stats
