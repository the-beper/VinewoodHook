// dinput8.dll proxy. The game loads it instead of the system DLL; it forwards DirectInput8Create
// to the real one, loads ScriptHookV.dll (our hook) early and, only if VinewoodHook.load_asi is
// present, also loads the .asi plugins (for setups with no other ASI loader).

#include <windows.h>

#include <string>

static HMODULE g_self = nullptr;
static HMODULE g_real = nullptr;

using DirectInput8Create_t = HRESULT(WINAPI*)(HINSTANCE, DWORD, const void*, void**, void*);

extern "C" HRESULT WINAPI DirectInput8Create(HINSTANCE inst, DWORD version, const void* riid, void** out,
                                             void* outer) {
    if (!g_real) {
        wchar_t dir[MAX_PATH];
        UINT n = GetSystemDirectoryW(dir, MAX_PATH);
        if (n == 0 || n >= MAX_PATH) return E_FAIL;
        g_real = LoadLibraryW((std::wstring(dir, n) + L"\\dinput8.dll").c_str());
        if (!g_real) return E_FAIL;
    }
    auto real = reinterpret_cast<DirectInput8Create_t>(reinterpret_cast<void*>(GetProcAddress(g_real, "DirectInput8Create")));
    return real ? real(inst, version, riid, out, outer) : E_FAIL;
}

static std::wstring own_dir() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return L"";
    std::wstring dir(path, n);
    dir.resize(dir.find_last_of(L"\\/") + 1);
    return dir;
}

static DWORD WINAPI load_thread(LPVOID) {
    const std::wstring dir = own_dir();
    LoadLibraryW((dir + L"ScriptHookV.dll").c_str());
    if (GetFileAttributesW((dir + L"VinewoodHook.load_asi").c_str()) == INVALID_FILE_ATTRIBUTES) return 0;

    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((dir + L"*.asi").c_str(), &fd);
    if (h == INVALID_HANDLE_VALUE) return 0;
    do {
        LoadLibraryW((dir + fd.cFileName).c_str());
    } while (FindNextFileW(h, &fd));
    FindClose(h);
    return 0;
}

BOOL APIENTRY DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        // Loading other DLLs from DllMain is not allowed, hence the thread.
        if (HANDLE t = CreateThread(nullptr, 0, load_thread, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
