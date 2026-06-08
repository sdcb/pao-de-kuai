#include "ai/PdkAiClient.h"

#include <cJSON.h>

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

std::string PrintJson(cJSON* json) {
    char* text = cJSON_PrintUnformatted(json);
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

std::string ToolArguments(const cJSON* message) {
    const cJSON* toolCalls = cJSON_GetObjectItemCaseSensitive(message, "tool_calls");
    if (cJSON_IsArray(toolCalls) && cJSON_GetArraySize(toolCalls) > 0) {
        const cJSON* toolCall = cJSON_GetArrayItem(toolCalls, 0);
        const cJSON* function = cJSON_GetObjectItemCaseSensitive(toolCall, "function");
        if (cJSON_IsObject(function)) {
            return JsonString(function, "arguments");
        }
    }

    const cJSON* functionCall = cJSON_GetObjectItemCaseSensitive(message, "function_call");
    if (cJSON_IsObject(functionCall)) {
        return JsonString(functionCall, "arguments");
    }
    return {};
}

bool ContainsOption(const std::vector<PdkAiOption>& options, int index) {
    for (const PdkAiOption& option : options) {
        if (option.index == index) {
            return true;
        }
    }
    return false;
}

cJSON* CreateMessage(const char* role, const std::string& content) {
    cJSON* message = cJSON_CreateObject();
    cJSON_AddStringToObject(message, "role", role);
    cJSON_AddStringToObject(message, "content", content.c_str());
    return message;
}

std::string BuildUserPrompt(const PdkAiRequest& request) {
    std::ostringstream out;
    out << request.contextText << "\n\n";
    out << "请选择一个候选项，只能调用 choose_move 函数。候选项如下：\n";
    for (const PdkAiOption& option : request.options) {
        out << option.index << ". " << option.summary << "\n";
    }
    out << "返回 index 为候选项编号；talk 可选，用一句中文短句表达。";
    return out.str();
}

} // namespace

PdkAiResponse PdkAiClient::Choose(const PdkAiRequest& request) const {
    PdkAiResponse result;
    if (request.provider.type != "openai") {
        result.errorMessage = "Unsupported AI provider type";
        return result;
    }
    if (request.provider.endpoint.empty() || request.provider.model.empty()) {
        result.errorMessage = "AI provider endpoint or model is empty";
        return result;
    }
    if (request.options.empty()) {
        result.errorMessage = "No AI options provided";
        return result;
    }

    const std::string requestBody = BuildRequestJson(request);
    if (requestBody.empty()) {
        result.errorMessage = "Failed to build AI request JSON";
        return result;
    }

    const HttpJsonResponse http = http_.Post(HttpJsonRequest{
        request.provider.endpoint,
        request.provider.apiKey,
        requestBody,
        request.logPath,
        30000
    });
    if (!http.ok) {
        result.rawResponse = http.body;
        result.errorMessage = http.errorMessage;
        return result;
    }
    return ParseResponse(http.body, request.options);
}

std::string PdkAiClient::BuildRequestJson(const PdkAiRequest& request) {
    JsonPtr root(cJSON_CreateObject());
    cJSON_AddStringToObject(root.get(), "model", request.provider.model.c_str());
    cJSON_AddBoolToObject(root.get(), "stream", false);
    cJSON_AddNumberToObject(root.get(), "temperature", 0.0);

    cJSON* messages = cJSON_CreateArray();
    cJSON_AddItemToArray(messages, CreateMessage("system", request.systemPrompt));
    cJSON_AddItemToArray(messages, CreateMessage("user", BuildUserPrompt(request)));
    cJSON_AddItemToObject(root.get(), "messages", messages);

    cJSON* parameters = cJSON_CreateObject();
    cJSON_AddStringToObject(parameters, "type", "object");
    cJSON* properties = cJSON_CreateObject();
    cJSON* index = cJSON_CreateObject();
    cJSON_AddStringToObject(index, "type", "integer");
    cJSON_AddStringToObject(index, "description", "The selected candidate option index.");
    cJSON_AddItemToObject(properties, "index", index);
    cJSON* talk = cJSON_CreateObject();
    cJSON_AddStringToObject(talk, "type", "string");
    cJSON_AddStringToObject(talk, "description", "Optional short Chinese table-talk line.");
    cJSON_AddItemToObject(properties, "talk", talk);
    cJSON_AddItemToObject(parameters, "properties", properties);
    cJSON* required = cJSON_CreateArray();
    cJSON_AddItemToArray(required, cJSON_CreateString("index"));
    cJSON_AddItemToObject(parameters, "required", required);

    cJSON* function = cJSON_CreateObject();
    cJSON_AddStringToObject(function, "name", "choose_move");
    cJSON_AddStringToObject(function, "description", "Choose one indexed move option.");
    cJSON_AddItemToObject(function, "parameters", parameters);

    cJSON* tool = cJSON_CreateObject();
    cJSON_AddStringToObject(tool, "type", "function");
    cJSON_AddItemToObject(tool, "function", function);
    cJSON* tools = cJSON_CreateArray();
    cJSON_AddItemToArray(tools, tool);
    cJSON_AddItemToObject(root.get(), "tools", tools);

    cJSON* toolChoice = cJSON_CreateObject();
    cJSON_AddStringToObject(toolChoice, "type", "function");
    cJSON* choiceFunction = cJSON_CreateObject();
    cJSON_AddStringToObject(choiceFunction, "name", "choose_move");
    cJSON_AddItemToObject(toolChoice, "function", choiceFunction);
    cJSON_AddItemToObject(root.get(), "tool_choice", toolChoice);
    return PrintJson(root.get());
}

PdkAiResponse PdkAiClient::ParseResponse(std::string responseBody, const std::vector<PdkAiOption>& options) {
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
    result.rawContent = JsonString(message, "content");
    result.reasoningContent = JsonString(message, "reasoning_content");
    if (result.reasoningContent.empty()) {
        result.reasoningContent = JsonString(message, "reasoningContent");
    }

    const std::string argumentsText = ToolArguments(message);
    if (argumentsText.empty()) {
        result.errorMessage = "AI response has no tool call arguments";
        return result;
    }
    JsonPtr arguments(cJSON_Parse(argumentsText.c_str()));
    if (!arguments || !cJSON_IsObject(arguments.get())) {
        result.errorMessage = "AI tool call arguments are not valid JSON";
        return result;
    }

    const cJSON* index = cJSON_GetObjectItemCaseSensitive(arguments.get(), "index");
    if (!cJSON_IsNumber(index)) {
        result.errorMessage = "AI tool call has no numeric index";
        return result;
    }
    result.selectedIndex = index->valueint;
    if (!ContainsOption(options, result.selectedIndex)) {
        result.errorMessage = "AI selected index is out of range";
        return result;
    }

    result.talk = JsonString(arguments.get(), "talk");
    result.ok = true;
    return result;
}

} // namespace pdk::ai
