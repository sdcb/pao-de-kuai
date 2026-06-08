#include "ai/PdkAiClient.h"

#include <cJSON.h>

#include <fstream>
#include <memory>
#include <sstream>

namespace pdk::ai {
namespace {

struct JsonDeleter {
    void operator()(cJSON* value) const {
        cJSON_Delete(value);
    }
};

using JsonPtr = std::unique_ptr<cJSON, JsonDeleter>;

std::string PrintJson(cJSON* json, bool formatted = false) {
    char* text = formatted ? cJSON_Print(json) : cJSON_PrintUnformatted(json);
    if (!text) {
        return {};
    }
    std::string result = text;
    cJSON_free(text);
    return result;
}

std::string JsonString(const cJSON* object, const char* key) {
    const cJSON* value = cJSON_GetObjectItemCaseSensitive(object, key);
    if (cJSON_IsString(value) && value->valuestring) {
        return value->valuestring;
    }
    return {};
}

void WriteTextFile(const std::filesystem::path& path, const std::string& text) {
    if (path.empty()) {
        return;
    }
    if (!path.parent_path().empty()) {
        std::filesystem::create_directories(path.parent_path());
    }
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out << text;
}

cJSON* CreateChooseMoveTool() {
    cJSON* parameters = cJSON_CreateObject();
    cJSON_AddStringToObject(parameters, "type", "object");

    cJSON* properties = cJSON_CreateObject();
    cJSON* action = cJSON_CreateObject();
    cJSON_AddStringToObject(action, "type", "string");
    cJSON* actionEnum = cJSON_CreateArray();
    cJSON_AddItemToArray(actionEnum, cJSON_CreateString("play"));
    cJSON_AddItemToArray(actionEnum, cJSON_CreateString("pass"));
    cJSON_AddItemToObject(action, "enum", actionEnum);
    cJSON_AddStringToObject(action, "description", "play 表示出牌，pass 表示不要。");
    cJSON_AddItemToObject(properties, "action", action);

    cJSON* ranks = cJSON_CreateObject();
    cJSON_AddStringToObject(ranks, "type", "array");
    cJSON* rankItems = cJSON_CreateObject();
    cJSON_AddStringToObject(rankItems, "type", "string");
    cJSON* rankEnum = cJSON_CreateArray();
    for (const char* rank : {"3", "4", "5", "6", "7", "8", "9", "10", "J", "Q", "K", "A", "2"}) {
        cJSON_AddItemToArray(rankEnum, cJSON_CreateString(rank));
    }
    cJSON_AddItemToObject(rankItems, "enum", rankEnum);
    cJSON_AddItemToObject(ranks, "items", rankItems);
    cJSON_AddStringToObject(ranks, "description", "要出的牌点数，不包含花色；对子/三张需要重复点数。pass 时留空。");
    cJSON_AddItemToObject(properties, "ranks", ranks);

    cJSON* talk = cJSON_CreateObject();
    cJSON_AddStringToObject(talk, "type", "string");
    cJSON_AddStringToObject(talk, "description", "可选中文短句。");
    cJSON_AddItemToObject(properties, "talk", talk);

    cJSON_AddItemToObject(parameters, "properties", properties);
    cJSON* required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("action"));
    cJSON_AddItemToObject(parameters, "required", required);

    cJSON* function = cJSON_CreateObject();
    cJSON_AddStringToObject(function, "name", "choose_move");
    cJSON_AddStringToObject(function, "description", "选择本手跑得快动作，返回动作与牌点数。");
    cJSON_AddItemToObject(function, "parameters", parameters);

    cJSON* tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddItemToObject(tool, "function", function);
    return tool;
}

cJSON* MessageToJson(const PdkAiMessage& message) {
    cJSON* json = cJSON_CreateObject();
    cJSON_AddStringToObject(json, "role", message.role.c_str());
    if (message.role == "tool") {
        cJSON_AddStringToObject(json, "tool_call_id", message.toolCallId.c_str());
        if (!message.name.empty()) {
            cJSON_AddStringToObject(json, "name", message.name.c_str());
        }
        cJSON_AddStringToObject(json, "content", message.content.c_str());
        return json;
    }

    if (message.role == "assistant" && !message.toolCalls.empty()) {
        cJSON_AddNullToObject(json, "content");
        if (!message.reasoningContent.empty()) {
            cJSON_AddStringToObject(json, "reasoning_content", message.reasoningContent.c_str());
        }
        cJSON* toolCalls = cJSON_CreateArray();
        for (const PdkAiToolCall& call : message.toolCalls) {
            cJSON* item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "id", call.id.c_str());
            cJSON_AddStringToObject(item, "type", "function");
            cJSON* function = cJSON_CreateObject();
            cJSON_AddStringToObject(function, "name", call.name.c_str());
            cJSON_AddStringToObject(function, "arguments", call.argumentsJson.c_str());
            cJSON_AddItemToObject(item, "function", function);
            cJSON_AddItemToArray(toolCalls, item);
        }
        cJSON_AddItemToObject(json, "tool_calls", toolCalls);
        return json;
    }

    cJSON_AddStringToObject(json, "content", message.content.c_str());
    return json;
}

const cJSON* FirstChoiceMessage(const cJSON* root) {
    const cJSON* choices = cJSON_GetObjectItemCaseSensitive(root, "choices");
    if (!cJSON_IsArray(choices) || cJSON_GetArraySize(choices) <= 0) {
        return nullptr;
    }
    const cJSON* choice = cJSON_GetArrayItem(choices, 0);
    if (!cJSON_IsObject(choice)) {
        return nullptr;
    }
    const cJSON* message = cJSON_GetObjectItemCaseSensitive(choice, "message");
    return cJSON_IsObject(message) ? message : nullptr;
}

std::string ToolArguments(const cJSON* message, PdkAiToolCall& parsed) {
    const cJSON* toolCalls = cJSON_GetObjectItemCaseSensitive(message, "tool_calls");
    if (!cJSON_IsArray(toolCalls) || cJSON_GetArraySize(toolCalls) <= 0) {
        return {};
    }

    const cJSON* toolCall = cJSON_GetArrayItem(toolCalls, 0);
    parsed.id = JsonString(toolCall, "id");
    const cJSON* function = cJSON_GetObjectItemCaseSensitive(toolCall, "function");
    if (!cJSON_IsObject(function)) {
        return {};
    }
    parsed.name = JsonString(function, "name");
    parsed.argumentsJson = JsonString(function, "arguments");
    return parsed.argumentsJson;
}

} // namespace

PdkAiResponse PdkAiClient::ChooseMove(const PdkAiRequest& request) const {
    PdkAiResponse result;
    if (request.provider.type != "openai") {
        result.errorMessage = "Unsupported AI provider type";
        return result;
    }
    if (request.provider.endpoint.empty() || request.provider.model.empty()) {
        result.errorMessage = "AI provider endpoint or model is empty";
        return result;
    }
    if (request.messages.empty()) {
        result.errorMessage = "No AI messages provided";
        return result;
    }

    const std::string requestBody = BuildRequestJson(request);
    if (requestBody.empty()) {
        result.errorMessage = "Failed to build AI request JSON";
        return result;
    }
    WriteTextFile(request.requestLogPath, requestBody);

    const HttpJsonResponse http = http_.Post(HttpJsonRequest{
        request.provider.endpoint,
        request.provider.apiKey,
        requestBody,
        {},
        request.timeoutMs
    });
    WriteTextFile(request.responseLogPath, http.body);
    if (!http.ok) {
        result.rawResponse = http.body;
        result.errorMessage = http.errorMessage;
        return result;
    }
    return ParseResponse(http.body);
}

std::string PdkAiClient::BuildRequestJson(const PdkAiRequest& request) {
    JsonPtr root(cJSON_CreateObject());
    cJSON_AddStringToObject(root.get(), "model", request.provider.model.c_str());
    cJSON_AddBoolToObject(root.get(), "stream", false);
    cJSON_AddNumberToObject(root.get(), "temperature", 0.0);

    cJSON* messages = cJSON_CreateArray();
    for (const PdkAiMessage& message : request.messages) {
        cJSON_AddItemToArray(messages, MessageToJson(message));
    }
    cJSON_AddItemToObject(root.get(), "messages", messages);

    cJSON* tools = cJSON_CreateArray();
    cJSON_AddItemToArray(tools, CreateChooseMoveTool());
    cJSON_AddItemToObject(root.get(), "tools", tools);

    cJSON* toolChoice = cJSON_CreateObject();
    cJSON_AddStringToObject(toolChoice, "type", "function");
    cJSON* choiceFunction = cJSON_CreateObject();
    cJSON_AddStringToObject(choiceFunction, "name", "choose_move");
    cJSON_AddItemToObject(toolChoice, "function", choiceFunction);
    cJSON_AddItemToObject(root.get(), "tool_choice", toolChoice);
    return PrintJson(root.get(), true);
}

PdkAiResponse PdkAiClient::ParseResponse(std::string responseBody) {
    PdkAiResponse result;
    result.rawResponse = std::move(responseBody);
    JsonPtr root(cJSON_Parse(result.rawResponse.c_str()));
    if (!root) {
        result.errorMessage = "AI response is not valid JSON";
        return result;
    }

    const cJSON* message = FirstChoiceMessage(root.get());
    if (!message) {
        result.errorMessage = "AI response has no choices[0].message";
        return result;
    }
    result.reasoningContent = JsonString(message, "reasoning_content");
    if (result.reasoningContent.empty()) {
        result.errorMessage = "AI response has no reasoning_content";
        return result;
    }

    PdkAiToolCall toolCall;
    const std::string argumentsText = ToolArguments(message, toolCall);
    if (argumentsText.empty()) {
        result.errorMessage = "AI response has no tool call arguments";
        return result;
    }
    if (toolCall.name != "choose_move") {
        result.errorMessage = "AI response tool call is not choose_move";
        return result;
    }

    JsonPtr arguments(cJSON_Parse(argumentsText.c_str()));
    if (!arguments || !cJSON_IsObject(arguments.get())) {
        result.errorMessage = "AI tool call arguments are not valid JSON";
        return result;
    }

    result.move.action = JsonString(arguments.get(), "action");
    if (result.move.action != "play" && result.move.action != "pass") {
        result.errorMessage = "AI tool call action is not play or pass";
        return result;
    }

    const cJSON* ranks = cJSON_GetObjectItemCaseSensitive(arguments.get(), "ranks");
    if (cJSON_IsArray(ranks)) {
        const cJSON* item = nullptr;
        cJSON_ArrayForEach(item, ranks) {
            if (cJSON_IsString(item) && item->valuestring) {
                result.move.ranks.emplace_back(item->valuestring);
            }
        }
    }
    result.move.talk = JsonString(arguments.get(), "talk");

    result.assistantMessage.role = "assistant";
    result.assistantMessage.reasoningContent = result.reasoningContent;
    result.assistantMessage.toolCalls = {toolCall};
    result.ok = true;
    return result;
}

std::string PdkAiClient::BuildToolResultJson(const PdkAiMove& move, bool accepted, const std::string& reason) {
    JsonPtr root(cJSON_CreateObject());
    cJSON_AddBoolToObject(root.get(), "accepted", accepted);
    cJSON_AddStringToObject(root.get(), "reason", reason.c_str());
    cJSON* action = cJSON_CreateObject();
    cJSON_AddStringToObject(action, "action", move.action.c_str());
    cJSON* ranks = cJSON_CreateArray();
    for (const std::string& rank : move.ranks) {
        cJSON_AddItemToArray(ranks, cJSON_CreateString(rank.c_str()));
    }
    cJSON_AddItemToObject(action, "ranks", ranks);
    if (!move.talk.empty()) {
        cJSON_AddStringToObject(action, "talk", move.talk.c_str());
    }
    cJSON_AddItemToObject(root.get(), "move", action);
    return PrintJson(root.get());
}

} // namespace pdk::ai
