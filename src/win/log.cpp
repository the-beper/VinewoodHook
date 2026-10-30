#include "win/log.h"

#include <cstdarg>
#include <cstdio>
#include <string>

static FILE* g_log = nullptr;

void log_init(HMODULE self) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return;
    std::wstring dir(path, n);
    dir.resize(dir.find_last_of(L"\\/") + 1);
    g_log = _wfopen((dir + L"VinewoodHook.log").c_str(), L"a");
}

void logf(const char* fmt, ...) {
    if (!g_log) return;
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(g_log, "[%02d:%02d:%02d] ", t.wHour, t.wMinute, t.wSecond);
    va_list ap;
    va_start(ap, fmt);
    std::vfprintf(g_log, fmt, ap);
    va_end(ap);
    std::fputc('\n', g_log);
    std::fflush(g_log);
}
