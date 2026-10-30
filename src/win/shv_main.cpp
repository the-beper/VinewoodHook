// ScriptHookV.dll: the hook proper. Finds the game internals, patches the script thread Tick and
// runs the scripts that .asi plugins register through the ScriptHookV-compatible API.

#include "win/diagnostics.h"
#include "win/explore.h"
#include "win/log.h"
#include "win/scripts.h"
#include "win/selftest.h"

#include <string>

static HMODULE g_self = nullptr;

// Marker files next to the DLL switch optional behaviour on.
static bool marker_exists(const wchar_t* name) {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(g_self, path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH) return false;
    std::wstring dir(path, n);
    dir.resize(dir.find_last_of(L"\\/") + 1);
    return GetFileAttributesW((dir + name).c_str()) != INVALID_FILE_ATTRIBUTES;
}

static DWORD WINAPI init_thread(LPVOID) {
    log_init(g_self);
    const vh::Edition edition = detect_edition();
    logf("VinewoodHook starting, edition %s", edition == vh::Edition::Enhanced ? "Enhanced" : "Legacy");

    // The executable may not be fully unpacked yet at this point (unverified), so retry
    // for a while before reporting.
    for (int attempt = 0; attempt < 30; ++attempt) {
        ScanReport r = run_diagnostics(edition, false);
        if (r.missing == 0 && r.ambiguous == 0) break;
        Sleep(1000);
    }
    ScanReport r = run_diagnostics(edition, true);
    logf("patterns: %d ok, %d missing, %d ambiguous, %d undefined", r.ok, r.missing, r.ambiguous, r.undefined);

    // The native table is filled while the game boots, so keep looking for a few minutes.
    bool found = false;
    for (int attempt = 0; attempt < 60 && !found; ++attempt) {
        Sleep(attempt == 0 ? 2000 : 5000);
        found = explore_game(edition);
    }
    if (!found) {
        logf("hook: native table never appeared, no scripts will run");
        return 0;
    }
    if (!install_frame_hook(edition, scripts::run_frame)) return 0;
    if (marker_exists(L"VinewoodHook.observe")) scripts::set_observe_only(true);
    if (marker_exists(L"VinewoodHook.probes.txt")) scripts::register_script(g_self, selftest_main);
    logf("hook: ready");

    if (marker_exists(L"VinewoodHook.dump")) {
        Sleep(30000); // let the game run a while so more code has been decrypted
        dump_module_image();
    }
    return 0;
}

BOOL APIENTRY DllMain(HINSTANCE inst, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = inst;
        DisableThreadLibraryCalls(inst);
        if (HANDLE t = CreateThread(nullptr, 0, init_thread, nullptr, 0, nullptr)) CloseHandle(t);
    }
    return TRUE;
}
