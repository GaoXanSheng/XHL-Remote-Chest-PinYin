#include "log.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>

static char g_path[MAX_PATH];

void log_init(void) {
    char exe[MAX_PATH];
    DWORD n = GetModuleFileNameA(NULL, exe, MAX_PATH);
    char *slash;
    if (n == 0 || n >= MAX_PATH) return;
    slash = strrchr(exe, '\\');
    if (!slash) return;
    *slash = 0;
    _snprintf_s(g_path, sizeof(g_path), _TRUNCATE, "%s\\XHL-Remote-Chest-PinYin.log", exe);
}

void log_msg(const char *fmt, ...) {
    char line[512];
    va_list ap;
    int n;
    HANDLE h;
    DWORD written;
    if (!g_path[0]) return;
    va_start(ap, fmt);
    n = snprintf(line, sizeof(line) - 3, "[%lu] ", (unsigned long)GetTickCount());
    if (n < 0) n = 0;
    n += vsnprintf(line + n, sizeof(line) - 3 - n, fmt, ap);
    va_end(ap);
    if (n < 0) return;
    if (n > (int)sizeof(line) - 3) n = (int)sizeof(line) - 3;
    line[n++] = '\r';
    line[n++] = '\n';
    line[n] = 0;
    h = CreateFileA(g_path, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (h == INVALID_HANDLE_VALUE) return;
    WriteFile(h, line, (DWORD)strlen(line), &written, NULL);
    CloseHandle(h);
}
