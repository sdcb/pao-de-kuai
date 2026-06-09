#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace pdk::core {

std::string CurrentDirectory();
std::string JoinPath(std::string_view lhs, std::string_view rhs);
std::string ParentPath(std::string_view path);
std::string FileStem(std::string_view path);
std::string FileExtension(std::string_view path);
bool DirectoryExists(std::string_view path);
bool CreateDirectories(std::string_view path);
std::string ReadTextFile(std::string_view path);
bool WriteTextFile(std::string_view path, std::string_view text);
std::vector<std::string> ListRegularFileNames(std::string_view directory);

} // namespace pdk::core
