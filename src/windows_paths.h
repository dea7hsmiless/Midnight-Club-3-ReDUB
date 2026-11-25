#ifndef MC3_WINDOWS_PATHS_H
#define MC3_WINDOWS_PATHS_H
#ifdef _WIN32
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

/* Host paths use UTF-8 in shared C code and UTF-16 at the Windows boundary.
 * Never round-trip them through the machine's ANSI code page. */
static inline int mc3_path_wide(const char *path, wchar_t *out, int capacity)
{
    return MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, out, capacity);
}
static inline int mc3_path_utf8(const wchar_t *path, char *out, int capacity)
{
    return WideCharToMultiByte(CP_UTF8, 0, path, -1, out, capacity, NULL, NULL);
}
static inline int mc3_module_path(char *out, int capacity)
{
    wchar_t path[32768];
    DWORD n = GetModuleFileNameW(NULL, path, 32768);
    return n && n < 32768 && mc3_path_utf8(path, out, capacity);
}
static inline const char *mc3_path_env(const wchar_t *name, char *out, int capacity)
{
    const wchar_t *value = _wgetenv(name);
    return value && mc3_path_utf8(value, out, capacity) ? out : NULL;
}
static inline int mc3_setenv_utf8(const char *name, const char *value)
{
    wchar_t key[256], text[32768];
    if (!mc3_path_wide(name, key, 256) || !mc3_path_wide(value, text, 32768)) return -1;
    return _wputenv_s(key, text);
}
static inline FILE *mc3_fopen_utf8(const char *path, const wchar_t *mode)
{
    wchar_t wide[32768];
    return mc3_path_wide(path, wide, 32768) ? _wfopen(wide, mode) : NULL;
}
static inline FILE *mc3_freopen_utf8(const char *path, const wchar_t *mode, FILE *stream)
{
    wchar_t wide[32768];
    return mc3_path_wide(path, wide, 32768) ? _wfreopen(wide, mode, stream) : NULL;
}
static inline DWORD mc3_attributes_utf8(const char *path)
{
    wchar_t wide[32768];
    return mc3_path_wide(path, wide, 32768) ? GetFileAttributesW(wide) : INVALID_FILE_ATTRIBUTES;
}
static inline int mc3_replace_utf8(const char *source, const char *dest)
{
    wchar_t from[32768], to[32768];
    return mc3_path_wide(source, from, 32768) && mc3_path_wide(dest, to, 32768) &&
        MoveFileExW(from, to, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
}
static inline void mc3_delete_utf8(const char *path)
{
    wchar_t wide[32768];
    if (mc3_path_wide(path, wide, 32768)) DeleteFileW(wide);
}
#endif
#endif
