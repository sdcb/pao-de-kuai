#pragma once

/*
 * Minimal Win32 file and path helpers, replacing the old std::string /
 * std::string_view / std::vector<std::string> API (plan.md 2).
 *
 * Conventions:
 *  - `out` parameters are cleared by the callee before being filled, so the
 *    caller only has to free them with Str_Free / StrList_Free.
 *  - Paths are normalised to backslashes on the way in, exactly as before.
 *  - Everything works on the process current directory, not the executable
 *    directory (AGENTS.md "资源与数据策略").
 */

#include "core/Str.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Falls back to "." when the current directory cannot be read. */
bool WinFile_CurrentDirectory(Str *out);

bool WinFile_JoinPath(Str *out, const char *lhs, const char *rhs);
/* Leaves `out` empty when the path has no parent component. */
void WinFile_ParentPath(Str *out, const char *path);
void WinFile_FileStem(Str *out, const char *path);
/* Leaves `out` empty when there is no extension (or the dot is in a directory). */
void WinFile_FileExtension(Str *out, const char *path);

bool WinFile_DirectoryExists(const char *path);
bool WinFile_CreateDirectories(const char *path);

/* Refuses files larger than 64 MiB, like the previous implementation. */
bool WinFile_ReadTextFile(const char *path, Str *out);
bool WinFile_WriteTextFile(const char *path, const char *text);

/* Regular files only (directories are skipped); `out` is appended to. */
bool WinFile_ListRegularFileNames(const char *directory, StrList *out);

#ifdef __cplusplus
}
#endif
