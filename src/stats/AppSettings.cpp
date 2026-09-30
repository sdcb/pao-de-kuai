#include "stats/AppSettings.h"

#include "core/WinFile.h"

#include <cJSON.h>

#include <algorithm>

namespace pdk::stats {
namespace {

float ClampFloat(double value, float min, float max) {
    return std::clamp(static_cast<float>(value), min, max);
}

} // namespace

std::string NormalizeAiSelection(const std::string& value) {
    return value == "strong" ? "strong" : "basic";
}

AppSettings LoadAppSettings(const std::string& path) {
    AppSettings settings;
    const std::string content = core::ReadTextFile(path);
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
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "ai1");
        cJSON_IsString(value) && value->valuestring) {
        settings.ai1 = NormalizeAiSelection(value->valuestring);
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "ai2");
        cJSON_IsString(value) && value->valuestring) {
        settings.ai2 = NormalizeAiSelection(value->valuestring);
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "roundTraceEnabled"); cJSON_IsBool(value)) {
        settings.roundTraceEnabled = cJSON_IsTrue(value);
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
    cJSON_AddStringToObject(root, "ai1", NormalizeAiSelection(settings.ai1).c_str());
    cJSON_AddStringToObject(root, "ai2", NormalizeAiSelection(settings.ai2).c_str());
    cJSON_AddBoolToObject(root, "roundTraceEnabled", settings.roundTraceEnabled);

    char* text = cJSON_Print(root);
    cJSON_Delete(root);
    if (!text) {
        return false;
    }

    const bool ok = core::WriteTextFile(path, text);
    cJSON_free(text);
    return ok;
}

} // namespace pdk::stats
