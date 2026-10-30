#pragma once

#include <cstdio>
#include <windows.h>

// Opens VinewoodHook.log next to our DLL (append mode).
void log_init(HMODULE self);
void logf(const char* fmt, ...) __attribute__((format(__MINGW_PRINTF_FORMAT, 1, 2)));
