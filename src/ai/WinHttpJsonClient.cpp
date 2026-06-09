#include "ai/WinHttpJsonClient.h"

#include "core/StringUtil.h"
#include "core/WinFile.h"

#include <cJSON.h>

#include <string_view>
#include <vector>

#include <windows.h>
#include <winhttp.h>

namespace pdk::ai {
namespace {

std::wstring Utf8ToWide(const std::string& value) {
    if (value.empty()) {
        return {};
    }
    const int count = MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), nullptr, 0);
    if (count <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(count), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.data(), static_cast<int>(value.size()), result.data(), count);
    return result;
}

std::string ErrorFromLastError(const char* operation) {
    std::string out = operation;
    out += " failed with Windows error ";
    core::AppendNumber(out, GetLastError());
    return out;
}

std::string SanitizeError(std::string error, const std::string& secret) {
    if (secret.empty()) {
        return error;
    }
    std::size_t pos = 0;
    while ((pos = error.find(secret, pos)) != std::string::npos) {
        error.replace(pos, secret.size(), "***");
        pos += 3;
    }
    return error;
}

std::string JsonEscape(std::string_view value) {
    std::string out;
    for (const unsigned char ch : value) {
        switch (ch) {
        case '\\':
            out += "\\\\";
            break;
        case '"':
            out += "\\\"";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (ch < 0x20) {
                constexpr char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[(ch >> 4) & 0x0F];
                out += hex[ch & 0x0F];
            } else {
                out += static_cast<char>(ch);
            }
            break;
        }
    }
    return out;
}

void AppendJsonString(std::string& out, const std::string& value) {
    out += '"';
    out += JsonEscape(value);
    out += '"';
}

void AppendJsonBody(std::string& out, const std::string& body, const std::string& secret) {
    if (body.empty()) {
        out += "null";
        return;
    }

    // Keep valid JSON payloads structured in the debug log, but fall back to a
    // string literal for plain-text errors so the log file is always parseable.
    std::string sanitized = SanitizeError(body, secret);
    cJSON* parsed = cJSON_Parse(sanitized.c_str());
    if (!parsed) {
        AppendJsonString(out, sanitized);
        return;
    }

    char* printed = cJSON_Print(parsed);
    cJSON_Delete(parsed);
    if (!printed) {
        AppendJsonString(out, sanitized);
        return;
    }
    out += printed;
    cJSON_free(printed);
}

struct ParsedUrl {
    std::wstring host;
    std::wstring path;
    INTERNET_PORT port{};
    bool secure{false};
};

bool ParseUrl(const std::string& endpoint, ParsedUrl& out) {
    const std::wstring wide = Utf8ToWide(endpoint);
    if (wide.empty()) {
        return false;
    }

    URL_COMPONENTS components{};
    components.dwStructSize = sizeof(components);
    components.dwSchemeLength = static_cast<DWORD>(-1);
    components.dwHostNameLength = static_cast<DWORD>(-1);
    components.dwUrlPathLength = static_cast<DWORD>(-1);
    components.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(wide.c_str(), static_cast<DWORD>(wide.size()), 0, &components)) {
        return false;
    }
    if (components.nScheme != INTERNET_SCHEME_HTTPS && components.nScheme != INTERNET_SCHEME_HTTP) {
        return false;
    }

    out.host.assign(components.lpszHostName, components.dwHostNameLength);
    out.path.assign(components.lpszUrlPath, components.dwUrlPathLength);
    if (components.dwExtraInfoLength > 0) {
        out.path.append(components.lpszExtraInfo, components.dwExtraInfoLength);
    }
    if (out.path.empty()) {
        out.path = L"/";
    }
    out.port = components.nPort;
    out.secure = components.nScheme == INTERNET_SCHEME_HTTPS;
    return !out.host.empty();
}

} // namespace

HttpJsonResponse WinHttpJsonClient::Post(const HttpJsonRequest& request) const {
    HttpJsonResponse response;
    ParsedUrl url;
    if (!ParseUrl(request.endpoint, url)) {
        response.errorMessage = "Invalid endpoint URL";
        WriteDebugLog(request, response);
        return response;
    }

    HINTERNET session = WinHttpOpen(
        L"pao-de-kuai/ai-client",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (!session) {
        response.errorMessage = ErrorFromLastError("WinHttpOpen");
        WriteDebugLog(request, response);
        return response;
    }

    const int timeout = request.timeoutMs > 0 ? request.timeoutMs : 30000;
    WinHttpSetTimeouts(session, timeout, timeout, timeout, timeout);

    HINTERNET connection = WinHttpConnect(session, url.host.c_str(), url.port, 0);
    if (!connection) {
        response.errorMessage = ErrorFromLastError("WinHttpConnect");
        WinHttpCloseHandle(session);
        WriteDebugLog(request, response);
        return response;
    }

    HINTERNET httpRequest = WinHttpOpenRequest(
        connection,
        L"POST",
        url.path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        url.secure ? WINHTTP_FLAG_SECURE : 0);
    if (!httpRequest) {
        response.errorMessage = ErrorFromLastError("WinHttpOpenRequest");
        WinHttpCloseHandle(connection);
        WinHttpCloseHandle(session);
        WriteDebugLog(request, response);
        return response;
    }

    std::wstring headers = L"Content-Type: application/json\r\n";
    if (!request.bearerToken.empty()) {
        headers += L"Authorization: Bearer ";
        headers += Utf8ToWide(request.bearerToken);
        headers += L"\r\n";
    }

    const BOOL sent = WinHttpSendRequest(
        httpRequest,
        headers.c_str(),
        static_cast<DWORD>(headers.size()),
        request.body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(request.body.data()),
        static_cast<DWORD>(request.body.size()),
        static_cast<DWORD>(request.body.size()),
        0);
    if (!sent || !WinHttpReceiveResponse(httpRequest, nullptr)) {
        response.errorMessage = ErrorFromLastError(sent ? "WinHttpReceiveResponse" : "WinHttpSendRequest");
    } else {
        DWORD statusCode = 0;
        DWORD statusSize = sizeof(statusCode);
        if (WinHttpQueryHeaders(
                httpRequest,
                WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                WINHTTP_HEADER_NAME_BY_INDEX,
                &statusCode,
                &statusSize,
                WINHTTP_NO_HEADER_INDEX)) {
            response.statusCode = static_cast<int>(statusCode);
        }

        while (true) {
            DWORD available = 0;
            if (!WinHttpQueryDataAvailable(httpRequest, &available)) {
                response.errorMessage = ErrorFromLastError("WinHttpQueryDataAvailable");
                break;
            }
            if (available == 0) {
                break;
            }

            std::vector<char> buffer(available);
            DWORD read = 0;
            if (!WinHttpReadData(httpRequest, buffer.data(), available, &read)) {
                response.errorMessage = ErrorFromLastError("WinHttpReadData");
                break;
            }
            response.body.append(buffer.data(), buffer.data() + read);
        }
        response.ok = response.statusCode >= 200 && response.statusCode < 300 && response.errorMessage.empty();
        if (!response.ok && response.errorMessage.empty()) {
            response.errorMessage = "HTTP status ";
            core::AppendNumber(response.errorMessage, response.statusCode);
        }
    }

    response.errorMessage = SanitizeError(std::move(response.errorMessage), request.bearerToken);
    WinHttpCloseHandle(httpRequest);
    WinHttpCloseHandle(connection);
    WinHttpCloseHandle(session);
    WriteDebugLog(request, response);
    return response;
}

void WinHttpJsonClient::WriteDebugLog(const HttpJsonRequest& request, const HttpJsonResponse& response) {
    if (request.logPath.empty()) {
        return;
    }
    std::string text;
    text += "{\n";
    text += "  \"endpoint\": ";
    AppendJsonString(text, SanitizeError(request.endpoint, request.bearerToken));
    text += ",\n";
    text += "  \"requestBody\": ";
    AppendJsonBody(text, request.body, request.bearerToken);
    text += ",\n";
    text += "  \"responseStatus\": ";
    core::AppendNumber(text, response.statusCode);
    text += ",\n";
    text += "  \"responseBody\": ";
    AppendJsonBody(text, response.body, request.bearerToken);
    text += ",\n";
    text += "  \"errorMessage\": ";
    AppendJsonString(text, SanitizeError(response.errorMessage, request.bearerToken));
    text += "\n";
    text += "}\n";
    core::WriteTextFile(request.logPath, text);
}

} // namespace pdk::ai
