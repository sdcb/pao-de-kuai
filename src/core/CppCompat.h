#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * This is plan.md 附录 B 路线 B: while `src/` is being converted, the modules
 * that are already pure C expose a plain C API, but the C++ translation units
 * that have not been converted yet keep calling the old `std::string`-shaped
 * API.  Rather than rewriting those call sites twice (once now, once when the
 * file itself becomes C), they include this header, which is a thin adapter:
 *
 *     std::string  <->  Str / StrList
 *     core::JoinPath(...)  ->  WinFile_JoinPath(...)
 *
 * Rules for this file:
 *   1. It is C++ only and must never be included from a .c file.
 *   2. It must stay header-only: no .cpp counterpart, nothing to link.
 *   3. It must NOT grow new features.  Every function here has a C twin, and it
 *      disappears together with the last C++ file under src/.
 *
 * Files still depending on it (shrink this list as they are converted):
 *   src/game/AiStrategy.cpp           src/overlays/SettingsOverlay.cpp
 *   src/game/GameState.cpp            src/overlays/RoundResultOverlay.cpp
 *   src/game/RoundTraceRecorder.cpp   src/scenes/GameScene.cpp
 *   src/scenes/StartScene.cpp         src/scenes/StatsScene.cpp
 *   src/stats/AppSettings.cpp         src/stats/StatStore.cpp
 */

#include <string>
#include <string_view>
#include <vector>

#include "core/Str.h"
#include "core/WinFile.h"

namespace pdk::core {

/* ---- Str <-> std::string --------------------------------------------- */

inline std::string FromStr(const Str& value)
{
    return std::string(Str_CStr(&value), static_cast<std::size_t>(value.len));
}

inline Str ToStr(std::string_view value)
{
    Str out;
    Str_Init(&out);
    Str_AppendN(&out, value.data(), static_cast<int>(value.size()));
    return out;
}

/* ---- number appending (was core/StringUtil.*) ------------------------ */

inline void AppendNumber(std::string& text, long long value)
{
    Str temp = ToStr(text);
    Str_AppendNumber(&temp, value);
    text.assign(Str_CStr(&temp), static_cast<std::size_t>(temp.len));
    Str_Free(&temp);
}

inline void AppendNumber(std::string& text, int value)
{
    AppendNumber(text, static_cast<long long>(value));
}

inline void AppendNumber(std::string& text, unsigned int value)
{
    Str temp = ToStr(text);
    Str_AppendUnsigned(&temp, value);
    text.assign(Str_CStr(&temp), static_cast<std::size_t>(temp.len));
    Str_Free(&temp);
}

inline void AppendNumber(std::string& text, unsigned long long value)
{
    Str temp = ToStr(text);
    Str_AppendUnsigned(&temp, value);
    text.assign(Str_CStr(&temp), static_cast<std::size_t>(temp.len));
    Str_Free(&temp);
}

inline void AppendPaddedNumber(std::string& text, int value, int width)
{
    Str temp = ToStr(text);
    Str_AppendPaddedNumber(&temp, value, width);
    text.assign(Str_CStr(&temp), static_cast<std::size_t>(temp.len));
    Str_Free(&temp);
}

/* ---- path and file helpers (was core/WinFile.*) ---------------------- */

inline std::string CurrentDirectory()
{
    Str out;
    Str_Init(&out);
    WinFile_CurrentDirectory(&out);
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline std::string JoinPath(std::string_view lhs, std::string_view rhs)
{
    Str out;
    Str_Init(&out);
    const std::string left(lhs);
    const std::string right(rhs);
    WinFile_JoinPath(&out, left.c_str(), right.c_str());
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline std::string ParentPath(std::string_view path)
{
    Str out;
    Str_Init(&out);
    const std::string value(path);
    WinFile_ParentPath(&out, value.c_str());
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline std::string FileStem(std::string_view path)
{
    Str out;
    Str_Init(&out);
    const std::string value(path);
    WinFile_FileStem(&out, value.c_str());
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline std::string FileExtension(std::string_view path)
{
    Str out;
    Str_Init(&out);
    const std::string value(path);
    WinFile_FileExtension(&out, value.c_str());
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline bool DirectoryExists(std::string_view path)
{
    const std::string value(path);
    return WinFile_DirectoryExists(value.c_str());
}

inline bool CreateDirectories(std::string_view path)
{
    const std::string value(path);
    return WinFile_CreateDirectories(value.c_str());
}

inline std::string ReadTextFile(std::string_view path)
{
    Str out;
    Str_Init(&out);
    const std::string value(path);
    WinFile_ReadTextFile(value.c_str(), &out);
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline bool WriteTextFile(std::string_view path, std::string_view text)
{
    const std::string value(path);
    const std::string body(text);
    return WinFile_WriteTextFile(value.c_str(), body.c_str());
}

inline std::vector<std::string> ListRegularFileNames(std::string_view directory)
{
    StrList list;
    StrList_Init(&list);
    const std::string value(directory);
    WinFile_ListRegularFileNames(value.c_str(), &list);
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(list.count));
    for (int i = 0; i < list.count; ++i) {
        result.emplace_back(StrList_At(&list, i));
    }
    StrList_Free(&list);
    return result;
}

} // namespace pdk::core
