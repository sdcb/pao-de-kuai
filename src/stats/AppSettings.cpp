#include "stats/AppSettings.h"

#include <cJSON.h>

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string_view>
#include <vector>
#include <windows.h>
#include <wincrypt.h>

namespace pdk::stats {
namespace {

constexpr std::string_view DpapiPrefix = "dpapi:";

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

std::string JsonString(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(value) && value->valuestring) {
        return value->valuestring;
    }
    return {};
}

std::string Base64Encode(const BYTE* data, DWORD size) {
    DWORD chars = 0;
    if (!CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &chars) || chars == 0) {
        return {};
    }
    std::string text(static_cast<std::size_t>(chars), '\0');
    if (!CryptBinaryToStringA(data, size, CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, text.data(), &chars)) {
        return {};
    }
    if (!text.empty() && text.back() == '\0') {
        text.pop_back();
    }
    return text;
}

std::vector<BYTE> Base64Decode(const std::string& text) {
    DWORD bytes = 0;
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, nullptr, &bytes, nullptr, nullptr) || bytes == 0) {
        return {};
    }
    std::vector<BYTE> data(static_cast<std::size_t>(bytes));
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()), CRYPT_STRING_BASE64, data.data(), &bytes, nullptr, nullptr)) {
        return {};
    }
    data.resize(bytes);
    return data;
}

std::string EncryptApiKey(const std::string& apiKey) {
    if (apiKey.empty() || apiKey.starts_with(DpapiPrefix)) {
        return apiKey;
    }

    DATA_BLOB input{};
    input.pbData = reinterpret_cast<BYTE*>(const_cast<char*>(apiKey.data()));
    input.cbData = static_cast<DWORD>(apiKey.size());
    DATA_BLOB output{};
    if (!CryptProtectData(&input, L"pao-de-kuai ai api key", nullptr, nullptr, nullptr, 0, &output)) {
        return apiKey;
    }

    const std::string encoded = Base64Encode(output.pbData, output.cbData);
    LocalFree(output.pbData);
    return encoded.empty() ? apiKey : std::string(DpapiPrefix) + encoded;
}

std::string DecryptApiKey(const std::string& text) {
    if (!text.starts_with(DpapiPrefix)) {
        return text;
    }

    const std::string encoded = text.substr(DpapiPrefix.size());
    std::vector<BYTE> encrypted = Base64Decode(encoded);
    if (encrypted.empty()) {
        return text;
    }

    DATA_BLOB input{};
    input.pbData = encrypted.data();
    input.cbData = static_cast<DWORD>(encrypted.size());
    DATA_BLOB output{};
    if (!CryptUnprotectData(&input, nullptr, nullptr, nullptr, nullptr, 0, &output)) {
        return text;
    }

    std::string decrypted(reinterpret_cast<char*>(output.pbData), reinterpret_cast<char*>(output.pbData) + output.cbData);
    LocalFree(output.pbData);
    return decrypted;
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
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "ai1");
        cJSON_IsString(value) && value->valuestring) {
        settings.ai1 = value->valuestring;
    }
    if (const cJSON* value = cJSON_GetObjectItemCaseSensitive(root, "ai2");
        cJSON_IsString(value) && value->valuestring) {
        settings.ai2 = value->valuestring;
    }
    const cJSON* providers = cJSON_GetObjectItemCaseSensitive(root, "aiProviders");
    if (cJSON_IsObject(providers)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, providers) {
            if (!cJSON_IsObject(item) || !item->string) {
                continue;
            }
            AiProviderSettings provider;
            provider.type = JsonString(item, "type");
            provider.endpoint = JsonString(item, "endpoint");
            provider.apiKey = DecryptApiKey(JsonString(item, "apiKey"));
            provider.model = JsonString(item, "model");
            settings.aiProviders[item->string] = std::move(provider);
        }
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
    cJSON_AddStringToObject(root, "ai1", settings.ai1.c_str());
    cJSON_AddStringToObject(root, "ai2", settings.ai2.c_str());
    if (!settings.aiProviders.empty()) {
        cJSON* providers = cJSON_CreateObject();
        for (const auto& [name, provider] : settings.aiProviders) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "type", provider.type.c_str());
            cJSON_AddStringToObject(item, "endpoint", provider.endpoint.c_str());
            const std::string storedApiKey = EncryptApiKey(provider.apiKey);
            cJSON_AddStringToObject(item, "apiKey", storedApiKey.c_str());
            cJSON_AddStringToObject(item, "model", provider.model.c_str());
            cJSON_AddItemToObject(providers, name.c_str(), item);
        }
        cJSON_AddItemToObject(root, "aiProviders", providers);
    }

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
