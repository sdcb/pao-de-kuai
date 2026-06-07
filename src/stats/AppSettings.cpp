#include "stats/AppSettings.h"

#include <cJSON.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace pdk::stats {
namespace {

std::string ReadFile(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return {};
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

float ClampFloat(double value, float min, float max) {
    return std::clamp(static_cast<float>(value), min, max);
}

} // namespace

AppSettings LoadAppSettings(const std::string& path) {
    AppSettings settings;
    const std::string content = ReadFile(path);
    if (content.empty()) {
        return settings;
    }

    cJSON* root = cJSON_Parse(content.c_str());
    if (!root) {
        return settings;
    }

    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "playerName");
        cJSON_IsString(value) && value->valuestring) {
        settings.playerName = value->valuestring;
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "masterVolume"); cJSON_IsNumber(value)) {
        settings.masterVolume = ClampFloat(value->valuedouble, 0.0f, 1.0f);
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "windowWidth"); cJSON_IsNumber(value)) {
        settings.windowWidth = std::max(1280, value->valueint);
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "windowHeight"); cJSON_IsNumber(value)) {
        settings.windowHeight = std::max(720, value->valueint);
    }

    cJSON_Delete(root);
    return settings;
}

bool SaveAppSettings(const AppSettings& settings, const std::string& path) {
    cJSON* root = cJSON_CreateObject();
    cJSON_AddStringToObject(root, "playerName", settings.playerName.c_str());
    cJSON_AddNumberToObject(root, "masterVolume", settings.masterVolume);
    cJSON_AddNumberToObject(root, "windowWidth", settings.windowWidth);
    cJSON_AddNumberToObject(root, "windowHeight", settings.windowHeight);

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) {
        return false;
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        cJSON_free(text);
        return false;
    }
    out << text;
    cJSON_free(text);
    return true;
}

} // namespace pdk::stats
