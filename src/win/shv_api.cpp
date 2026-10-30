// The ScriptHookV C++ API (SDK main.h), exported under the same decorated names so that existing
// .asi plugins import it unchanged. The functions are plain extern "C"; ScriptHookV.def maps the
// decorated names onto them.

#include "win/diagnostics.h"
#include "win/explore.h"
#include "win/log.h"
#include "win/scripts.h"

#include <atomic>

namespace {

// getGameVersion() returns an index into ScriptHookV's eGameVersion. Plugins only compare it with
// ">=" against known builds and treat 1000 and above as Enhanced. Legacy: 101 is 1.0.3788.0, the
// last build in the public enums, the installed 3889 is newer. Enhanced: 1009 is 2.0.1013.33, the
// installed 1158 is newer. The exact numbers for those two builds are not known.
constexpr int kLegacyVersion = 105;
constexpr int kEnhancedVersion = 1010;

void log_once(std::atomic<bool>& flag, const char* what) {
    if (!flag.exchange(true)) logf("shv: %s is not implemented (returning an empty result)", what);
}

} // namespace

extern "C" {

void vh_scriptRegister(HMODULE module, void (*main)()) { scripts::register_script(module, main); }
void vh_scriptRegisterAdditionalThread(HMODULE module, void (*main)()) { scripts::register_script(module, main); }
void vh_scriptUnregisterMain(void (*main)()) { scripts::unregister(main); }
void vh_scriptUnregisterModule(HMODULE module) { scripts::unregister(module); }
void vh_scriptWait(DWORD ms) { scripts::wait(ms); }
bool vh_scriptsAreLaunchedUsingReloading() { return false; }
bool vh_nativeCanExecuteInThisContext() { return scripts::in_script(); }

void vh_nativeInit(uint64_t hash) { scripts::native_init(hash); }
void vh_nativePush64(uint64_t value) { scripts::native_push(value); }
uint64_t* vh_nativeCall() { return scripts::native_call(); }

void vh_keyboardHandlerRegister(scripts::KeyboardHandler h) { scripts::keyboard_register(h); }
void vh_keyboardHandlerUnregister(scripts::KeyboardHandler h) { scripts::keyboard_unregister(h); }

int vh_getGameVersion() { return detect_edition() == vh::Edition::Enhanced ? kEnhancedVersion : kLegacyVersion; }

// getGlobalPtr(id): block (id >> 18) & 0x3F, slot id & 0x3FFFF. Where the table is unknown a large
// zeroed buffer stands in, so that scripts read zeros instead of dereferencing null.
uint64_t* vh_getGlobalPtr(int id) {
    const uint32_t block = (static_cast<uint32_t>(id) >> 18) & 0x3F, slot = static_cast<uint32_t>(id) & 0x3FFFF;
    if (const uint64_t table = script_globals_table()) {
        auto** blocks = reinterpret_cast<uint64_t**>(table);
        if (blocks[block]) return &blocks[block][slot];
    }
    static uint64_t* fallback = static_cast<uint64_t*>(
        VirtualAlloc(nullptr, 64ull * 0x40000 * 8, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE));
    static std::atomic<bool> once;
    log_once(once, "getGlobalPtr (no global table, serving zeros)");
    return fallback ? &fallback[static_cast<uint64_t>(block) * 0x40000 + slot] : nullptr;
}

// Entity pointer for a script handle; null for anything that does not look like a live object.
uint8_t* vh_getScriptHandleBaseAddress(int handle) {
    const uint64_t fn = script_entity_function();
    if (!fn || handle == 0) return nullptr;
    static std::atomic<int> traced{0};
    const bool trace = traced.fetch_add(1) < 8;
    if (trace) logf("shv: getScriptHandleBaseAddress(0x%X) calling %p", handle, reinterpret_cast<void*>(fn));
    const auto entity = reinterpret_cast<uint64_t (*)(int)>(fn)(handle);
    if (trace) logf("shv: getScriptHandleBaseAddress(0x%X) -> %llX", handle, (unsigned long long)entity);
    uint64_t probe;
    SIZE_T got = 0;
    if (entity < 0x10000 || entity >= (1ull << 47) ||
        !ReadProcessMemory(GetCurrentProcess(), reinterpret_cast<LPCVOID>(entity), &probe, sizeof probe, &got) || got != sizeof probe)
        return nullptr;
    return reinterpret_cast<uint8_t*>(entity);
}
int vh_worldGetAll(int*, int) {
    static std::atomic<bool> once;
    log_once(once, "worldGetAll*");
    return 0;
}
void vh_presentCallbackRegister(void (*)(void*)) {
    static std::atomic<bool> once;
    log_once(once, "presentCallbackRegister (callbacks will never run)");
}
void vh_presentCallbackUnregister(void (*)(void*)) {}
int vh_createTexture(const char*) {
    static std::atomic<bool> once;
    log_once(once, "createTexture");
    return -1;
}
void vh_drawTexture(int, int, int, int, float, float, float, float, float, float, float, float, float, float) {}

} // extern "C"
