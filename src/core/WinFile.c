#include "core/WinFile.h"

#include <string.h>

#include <windows.h>

/*
 * Ported from the previous C++ implementation without changing behaviour:
 *  - '/' is normalised to '\\' everywhere;
 *  - RootOffset skips "X:\" and "\\server\share\" so CreateDirectories does not
 *    try to create the drive root or the UNC share;
 *  - reading refuses files above 64 MiB.
 */

#define PDK_MAX_TEXT_FILE_BYTES (64ll * 1024ll * 1024ll)

static bool IsSlash(char ch)
{
    return ch == '\\' || ch == '/';
}

/* Copies `path` into `out` with all slashes normalised to backslashes. */
static void NormalizePath(Str *out, const char *path)
{
    Str_Clear(out);
    if (path == NULL) {
        return;
    }
    const int len = (int)strlen(path);
    if (!Str_Reserve(out, len) || out->data == NULL) {
        return;
    }
    for (int i = 0; i < len; ++i) {
        out->data[i] = path[i] == '/' ? '\\' : path[i];
    }
    out->data[len] = '\0';
    out->len = len;
}

/* Number of leading characters that identify the volume rather than a
 * directory inside it. */
static int RootOffset(const char *path, int len)
{
    if (len >= 3 && path[1] == ':' && IsSlash(path[2])) {
        return 3;
    }
    if (len >= 2 && IsSlash(path[0]) && IsSlash(path[1])) {
        int pos = -1;
        for (int i = 2; i < len; ++i) {
            if (path[i] == '\\') {
                pos = i;
                break;
            }
        }
        if (pos < 0) {
            return len;
        }
        for (int i = pos + 1; i < len; ++i) {
            if (path[i] == '\\') {
                return i + 1;
            }
        }
        return len;
    }
    return 0;
}

bool WinFile_CurrentDirectory(Str *out)
{
    DWORD length;
    DWORD written;

    Str_Clear(out);
    length = GetCurrentDirectoryA(0, NULL);
    if (length == 0) {
        Str_Assign(out, ".");
        return false;
    }
    if (!Str_Reserve(out, (int)length) || out->data == NULL) {
        return false;
    }
    written = GetCurrentDirectoryA(length, out->data);
    if (written == 0 || written >= length) {
        Str_Clear(out);
        Str_Assign(out, ".");
        return false;
    }
    out->len = (int)written;
    return true;
}

bool WinFile_JoinPath(Str *out, const char *lhs, const char *rhs)
{
    bool needsSeparator;

    if (lhs == NULL || lhs[0] == '\0') {
        NormalizePath(out, rhs != NULL ? rhs : "");
        return true;
    }
    if (rhs == NULL || rhs[0] == '\0') {
        NormalizePath(out, lhs);
        return true;
    }

    NormalizePath(out, lhs);
    if (out->data == NULL) {
        return false;
    }
    needsSeparator = out->len > 0 && !IsSlash(out->data[out->len - 1]);
    if (needsSeparator) {
        Str_AppendChar(out, '\\');
    }
    /* Skip any leading separators on the right-hand side so that joining with
     * "\stat" does not produce a double backslash. */
    while (*rhs != '\0' && IsSlash(*rhs)) {
        ++rhs;
    }
    for (; *rhs != '\0'; ++rhs) {
        Str_AppendChar(out, *rhs == '/' ? '\\' : *rhs);
    }
    return out->data != NULL;
}

void WinFile_ParentPath(Str *out, const char *path)
{
    Str value;
    int end;
    int slash;

    Str_Init(&value);
    NormalizePath(&value, path);
    end = value.len;
    while (end > 0 && IsSlash(value.data[end - 1])) {
        --end;
    }
    slash = -1;
    for (int i = end - 1; i >= 0; --i) {
        if (IsSlash(value.data[i])) {
            slash = i;
            break;
        }
    }
    if (slash >= 0) {
        Str_AssignN(out, value.data, slash);
    } else {
        Str_Clear(out);
    }
    Str_Free(&value);
}

void WinFile_FileStem(Str *out, const char *path)
{
    const char *value;
    const char *name;
    const char *dot;

    Str_Clear(out);
    if (path == NULL) {
        return;
    }
    value = path;
    name = value;
    for (const char *p = value; *p != '\0'; ++p) {
        if (IsSlash(*p)) {
            name = p + 1;
        }
    }
    dot = NULL;
    for (const char *p = name; *p != '\0'; ++p) {
        if (*p == '.') {
            dot = p;
        }
    }
    if (dot != NULL) {
        Str_AppendN(out, name, (int)(dot - name));
    } else {
        Str_Append(out, name);
    }
}

void WinFile_FileExtension(Str *out, const char *path)
{
    const char *value;
    const char *slash;
    const char *dot;

    Str_Clear(out);
    if (path == NULL) {
        return;
    }
    value = path;
    slash = NULL;
    dot = NULL;
    for (const char *p = value; *p != '\0'; ++p) {
        if (IsSlash(*p)) {
            slash = p;
        } else if (*p == '.') {
            dot = p;
        }
    }
    if (dot == NULL || (slash != NULL && dot < slash)) {
        return;
    }
    Str_Append(out, dot);
}

bool WinFile_DirectoryExists(const char *path)
{
    Str value;
    DWORD attrs;
    bool exists;

    Str_Init(&value);
    NormalizePath(&value, path);
    if (value.data == NULL) {
        Str_Free(&value);
        return false;
    }
    attrs = GetFileAttributesA(value.data);
    exists = attrs != INVALID_FILE_ATTRIBUTES &&
             (attrs & FILE_ATTRIBUTE_DIRECTORY) != 0;
    Str_Free(&value);
    return exists;
}

bool WinFile_CreateDirectories(const char *path)
{
    Str value;
    int start;
    bool ok = true;

    Str_Init(&value);
    NormalizePath(&value, path);
    if (value.data == NULL || value.len == 0 || WinFile_DirectoryExists(path)) {
        Str_Free(&value);
        return true;
    }
    while (value.len > 0 && IsSlash(value.data[value.len - 1])) {
        value.data[--value.len] = '\0';
    }

    start = RootOffset(value.data, value.len);
    for (int pos = start; pos <= value.len; ++pos) {
        char saved;
        if (pos != value.len && !IsSlash(value.data[pos])) {
            continue;
        }
        saved = value.data[pos];
        value.data[pos] = '\0';
        if (value.data[0] != '\0' && !WinFile_DirectoryExists(value.data)) {
            if (!CreateDirectoryA(value.data, NULL) &&
                GetLastError() != ERROR_ALREADY_EXISTS) {
                value.data[pos] = saved;
                Str_Free(&value);
                return false;
            }
        }
        value.data[pos] = saved;
    }
    ok = WinFile_DirectoryExists(value.data);
    Str_Free(&value);
    return ok;
}

bool WinFile_ReadTextFile(const char *path, Str *out)
{
    Str value;
    HANDLE file;
    LARGE_INTEGER size;
    DWORD read = 0;
    BOOL ok;

    Str_Clear(out);
    Str_Init(&value);
    NormalizePath(&value, path);
    if (value.data == NULL) {
        Str_Free(&value);
        return false;
    }

    file = CreateFileA(value.data, GENERIC_READ, FILE_SHARE_READ, NULL,
                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        Str_Free(&value);
        return false;
    }
    if (!GetFileSizeEx(file, &size) || size.QuadPart <= 0 ||
        size.QuadPart > PDK_MAX_TEXT_FILE_BYTES) {
        CloseHandle(file);
        Str_Free(&value);
        return false;
    }
    if (!Str_Reserve(out, (int)size.QuadPart)) {
        CloseHandle(file);
        Str_Free(&value);
        return false;
    }
    ok = ReadFile(file, out->data, (DWORD)size.QuadPart, &read, NULL);
    CloseHandle(file);
    Str_Free(&value);
    if (!ok) {
        Str_Clear(out);
        return false;
    }
    out->len = (int)read;
    out->data[out->len] = '\0';
    return true;
}

bool WinFile_WriteTextFile(const char *path, const char *text)
{
    Str value;
    Str parent;
    HANDLE file;
    DWORD written = 0;
    DWORD length;
    BOOL ok;

    Str_Init(&value);
    Str_Init(&parent);
    NormalizePath(&value, path);
    if (value.data == NULL) {
        Str_Free(&value);
        return false;
    }
    WinFile_ParentPath(&parent, value.data);
    if (parent.len > 0 && !WinFile_CreateDirectories(parent.data)) {
        Str_Free(&parent);
        Str_Free(&value);
        return false;
    }
    Str_Free(&parent);

    file = CreateFileA(value.data, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
                       FILE_ATTRIBUTE_NORMAL, NULL);
    if (file == INVALID_HANDLE_VALUE) {
        Str_Free(&value);
        return false;
    }
    length = text != NULL ? (DWORD)strlen(text) : 0;
    ok = WriteFile(file, text != NULL ? text : "", length, &written, NULL);
    CloseHandle(file);
    Str_Free(&value);
    return ok && written == length;
}

bool WinFile_ListRegularFileNames(const char *directory, StrList *out)
{
    Str pattern;
    WIN32_FIND_DATAA data;
    HANDLE find;

    Str_Init(&pattern);
    if (!WinFile_JoinPath(&pattern, directory, "*") || pattern.data == NULL) {
        Str_Free(&pattern);
        return false;
    }
    find = FindFirstFileA(pattern.data, &data);
    Str_Free(&pattern);
    if (find == INVALID_HANDLE_VALUE) {
        return false;
    }
    do {
        if ((data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0) {
            if (!StrList_Push(out, data.cFileName, -1)) {
                break;
            }
        }
    } while (FindNextFileA(find, &data));
    FindClose(find);
    /* An existing directory with no regular files is success with an empty
     * list, not a failure -- callers must not treat it as "cannot read". */
    return true;
}
