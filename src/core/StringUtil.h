#pragma once

#include <string>

namespace pdk::core {

void AppendNumber(std::string& text, int value);
void AppendNumber(std::string& text, unsigned int value);
void AppendNumber(std::string& text, long value);
void AppendNumber(std::string& text, unsigned long value);
void AppendNumber(std::string& text, long long value);
void AppendNumber(std::string& text, unsigned long long value);
void AppendPaddedNumber(std::string& text, int value, int width);

} // namespace pdk::core
