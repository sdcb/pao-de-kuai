#pragma once

#include <map>
#include <string>

namespace pdk::stats {

struct AiProviderSettings {
    std::string type;
    std::string endpoint;
    std::string apiKey;
    std::string model;
};

struct AppSettings {
    std::string playerName{"\xE6\x9D\x8E\xE5\xA7\x90"};
    float masterVolume{0.8f};
    int windowWidth{1280};
    int windowHeight{720};
    std::string ai1{"basic"};
    std::string ai2{"basic"};
    bool roundTraceEnabled{false};
    std::map<std::string, AiProviderSettings> aiProviders;
};

AppSettings LoadAppSettings(const std::string& path = "appsettings.json");
bool SaveAppSettings(const AppSettings& settings, const std::string& path = "appsettings.json");

} // namespace pdk::stats
