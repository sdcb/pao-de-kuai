#include "core/WinFile.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif

#include <algorithm>
#include <windows.h>

namespace pdk::core {
namespace {

bool IsSlash(char ch) {
    return ch == '\\' || ch == '/';
}

std::string NormalizePath(std::string_view path) {
    std::string result(path);
    std::replace(result.begin(), result.end(), '/', '\\');
    return result;
}

std::size_t RootOffset(const std::string& path) {
    if (path.size() >= 3 && path[1] == ':' && IsSlash(path[2])) {
        return 3;
    }
    if (path.size() >= 2 && IsSlash(path[0]) && IsSlash(path[1])) {
        std::size_t pos = path.find('\\', 2);
        if (pos == std::string::npos) {
            return path.size();
        }
        pos = path.find('\\', pos + 1);
        return pos == std::string::npos ? path.size() : pos + 1;
    }
    return 0;
}

} // namespace

std::string CurrentDirectory() {
    DWORD length = GetCurrentDirectoryA(0, nullptr);
    if (length == 0) {
        return ".";
    }
    std::string path(length, '\0');
    DWORD written = GetCurrentDirectoryA(length, path.data());
    if (written == 0 || written >= length) {
        return ".";
    }
    path.resize(written);
    return path;
}

std::string JoinPath(std::string_view lhs, std::string_view rhs) {
    if (lhs.empty()) {
        return NormalizePath(rhs);
    }
    if (rhs.empty()) {
        return NormalizePath(lhs);
    }
    std::string result = NormalizePath(lhs);
    if (!IsSlash(result.back())) {
        result += '\\';
    }
    std::string right = NormalizePath(rhs);
    while (!right.empty() && IsSlash(right.front())) {
        right.erase(right.begin());
    }
    result += right;
    return result;
}

std::string ParentPath(std::string_view path) {
    std::string value = NormalizePath(path);
    while (!value.empty() && IsSlash(value.back())) {
        value.pop_back();
    }
    const std::size_t pos = value.find_last_of("\\/");
    if (pos == std::string::npos) {
        return {};
    }
    return value.substr(0, pos);
}

std::string FileStem(std::string_view path) {
    std::string value(path);
    const std::size_t slash = value.find_last_of("\\/");
    std::string name = slash == std::string::npos ? value : value.substr(slash + 1);
    const std::size_t dot = name.find_last_of('.');
    return dot == std::string::npos ? name : name.substr(0, dot);
}

std::string FileExtension(std::string_view path) {
    std::string value(path);
    const std::size_t slash = value.find_last_of("\\/");
    const std::size_t dot = value.find_last_of('.');
    if (dot == std::string::npos || (slash != std::string::npos && dot < slash)) {
        return {};
    }
    return value.substr(dot);
}

bool DirectoryExists(std::string_view path) {
    const std::string value = NormalizePath(path);
    const DWORD attrs = GetFileAttributesA(value.c_str());
    return attrs != INVALID_FILE_ATTRIBUTES && (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
}

bool CreateDirectories(std::string_view path) {
    std::string value = NormalizePath(path);
    if (value.empty() || DirectoryExists(value)) {
        return true;
    }
    while (!value.empty() && IsSlash(value.back())) {
        value.pop_back();
    }

    std::size_t start = RootOffset(value);
    for (std::size_t pos = start; pos <= value.size(); ++pos) {
        if (pos != value.size() && !IsSlash(value[pos])) {
            continue;
        }
        const std::string part = value.substr(0, pos);
        if (part.empty() || DirectoryExists(part)) {
            continue;
        }
        if (!CreateDirectoryA(part.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) {
            return false;
        }
    }
    return DirectoryExists(value);
}

std::string ReadTextFile(std::string_view path) {
    const std::string value = NormalizePath(path);
    HANDLE file = CreateFileA(value.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return {};
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0) {
        CloseHandle(file);
        return {};
    }
    if (size.QuadPart > 64ll * 1024ll * 1024ll) {
        CloseHandle(file);
        return {};
    }
    std::string result(static_cast<std::size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const BOOL ok = ReadFile(file, result.data(), static_cast<DWORD>(result.size()), &read, nullptr);
    CloseHandle(file);
    if (!ok) {
        return {};
    }
    result.resize(read);
    return result;
}

bool WriteTextFile(std::string_view path, std::string_view text) {
    const std::string value = NormalizePath(path);
    const std::string parent = ParentPath(value);
    if (!parent.empty() && !CreateDirectories(parent)) {
        return false;
    }
    HANDLE file = CreateFileA(value.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        return false;
    }
    DWORD written = 0;
    const BOOL ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr);
    CloseHandle(file);
    return ok && written == text.size();
}

std::vector<std::string> ListRegularFileNames(std::string_view directory) {
    std::vector<std::string> names;
    const std::string pattern = JoinPath(directory, "*");
    WIN32_FIND_DATAA data{};
    HANDLE find = FindFirstFileA(pattern.c_str(), &data);
    if (find == INVALID_HANDLE_VALUE) {
        return names;
    }
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            names.emplace_back(data.cFileName);
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
    return names;
}

} // namespace pdk::core
