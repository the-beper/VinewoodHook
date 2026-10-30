#include "win/scripts.h"

#include "win/explore.h"
#include "win/log.h"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <exception>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace scripts {

namespace {

constexpr size_t kFiberStack = 1 << 20;
constexpr size_t kMaxArgs = 32;

struct Script {
    HMODULE module;
    ScriptMain main;
    LPVOID fiber = nullptr;
    DWORD wake = 0;
    bool dead = false;
};

Script* g_current = nullptr;
thread_local uint64_t t_hash = 0;
thread_local void* t_recover[5];       // __builtin_setjmp buffer, valid while t_recovering
thread_local bool t_recovering = false;
std::unordered_map<uint64_t, int> g_native_crashes;
constexpr int kMaxNativeCrashes = 3;   // after this many faults a native is no longer called
std::mutex g_lock; // guards g_scripts and g_keyboard (scripts register from loader threads)
std::vector<std::unique_ptr<Script>> g_scripts;
std::vector<KeyboardHandler> g_keyboard;

// The last native calls of the running script, dumped by the watchdog when a script is stuck.
struct CallTrace {
    uint64_t hash;
    uint32_t argc;
    uint64_t arg0, arg1, arg2;
    uint64_t result;
};
constexpr size_t kTraceSize = 48;
CallTrace g_trace[kTraceSize];
std::atomic<uint32_t> g_trace_pos{0};

std::atomic<DWORD> g_main_thread_id{0};
std::atomic<DWORD> g_run_started{0}; // tick count when the current script was resumed, 0 when none
std::atomic<Script*> g_watched{nullptr};

std::string module_name(HMODULE m) {
    char path[MAX_PATH];
    DWORD n = GetModuleFileNameA(m, path, MAX_PATH);
    std::string p(path, n < MAX_PATH ? n : 0);
    return p.substr(p.find_last_of("\\/") + 1);
}

// A script that never calls scriptWait freezes the game. Nothing can preempt it, but naming it in
// the log makes the culprit obvious.
DWORD WINAPI watchdog(LPVOID) {
    Script* reported = nullptr;
    uint32_t last_pos = 0;
    int extra = 0;
    for (;;) {
        Sleep(1000);
        Script* s = g_watched.load();
        const DWORD started = g_run_started.load();
        if (!s || !started) { reported = nullptr; extra = 0; continue; }
        if (GetTickCount() - started <= 3000) continue;
        const uint32_t end = g_trace_pos.load();
        if (s != reported) {
            reported = s;
            extra = 0;
            logf("scripts: %s (main %p) has been running for over 3 s without waiting", module_name(s->module).c_str(),
                 reinterpret_cast<void*>(s->main));
            for (uint32_t i = end > 24 ? end - 24 : 0; i < end; ++i) {
                const CallTrace& t = g_trace[i % kTraceSize];
                logf("scripts:   native 0x%016llX argc %u args %llX %llX %llX -> %llX", (unsigned long long)t.hash, t.argc,
                     (unsigned long long)t.arg0, (unsigned long long)t.arg1, (unsigned long long)t.arg2,
                     (unsigned long long)t.result);
            }
        } else if (extra < 6) {
            ++extra;
            logf("scripts:   still running, %u native calls in the last second", end - last_pos);
        }
        last_pos = end;
    }
}

LPVOID g_main_fiber = nullptr;
DWORD g_main_thread = 0;

// Mirrors the layout the game's native handlers expect (checked against SQRT on both editions).
struct NativeContext {
    uint64_t* ret;
    uint32_t argc;
    uint32_t pad0;
    uint64_t* args;
    uint32_t data_count;
    uint32_t pad1;
    alignas(8) uint8_t vector_space[192];
    uint64_t stack[kMaxArgs];
};

using NativeHandler = void (*)(NativeContext*);
using SetVectorResultsFn = void (*)(NativeContext*);

thread_local NativeContext t_ctx;
thread_local NativeHandler t_handler = nullptr;

std::string describe_address(uint64_t addr) {
    HMODULE m = nullptr;
    if (!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCSTR>(addr), &m) || !m)
        return "?";
    char buf[96];
    std::snprintf(buf, sizeof buf, "%s+0x%llX", module_name(m).c_str(),
                  (unsigned long long)(addr - reinterpret_cast<uint64_t>(m)));
    return buf;
}

// Logs severe exceptions (access violations and the like) with their location, then lets the normal
// handling carry on. Without it a crash inside a script leaves no trace in our log.
LONG CALLBACK exception_logger(EXCEPTION_POINTERS* info) {
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if ((code & 0xF0000000) != 0xC0000000) return EXCEPTION_CONTINUE_SEARCH;
    if (t_recovering && code == EXCEPTION_ACCESS_VIOLATION) {
        // A native faulted: note it and resume right after the call, as if it had returned nothing.
        const int n = ++g_native_crashes[t_hash];
        const auto addr = reinterpret_cast<uint64_t>(info->ExceptionRecord->ExceptionAddress);
        logf("scripts: native 0x%016llX faulted at %s (access to %llX), returning 0 (fault %d of %d)",
             (unsigned long long)t_hash, describe_address(addr).c_str(),
             (unsigned long long)info->ExceptionRecord->ExceptionInformation[1], n, kMaxNativeCrashes);
        t_recovering = false;
        __builtin_longjmp(t_recover, 1);
    }
    static std::atomic<int> reported{0};
    if (reported.fetch_add(1) >= 12) return EXCEPTION_CONTINUE_SEARCH;
    const auto addr = reinterpret_cast<uint64_t>(info->ExceptionRecord->ExceptionAddress);
    logf("scripts: exception 0x%08lX at %016llX (%s) thread %lu, in script: %s", code, (unsigned long long)addr,
         describe_address(addr).c_str(), GetCurrentThreadId(), g_current ? module_name(g_current->module).c_str() : "no");
    if (code == EXCEPTION_ACCESS_VIOLATION && info->ExceptionRecord->NumberParameters >= 2)
        logf("scripts:   %s address %016llX", info->ExceptionRecord->ExceptionInformation[0] ? "write to" : "read of",
             (unsigned long long)info->ExceptionRecord->ExceptionInformation[1]);
    logf("scripts:   native in progress: 0x%016llX (argc %u)", (unsigned long long)t_hash, t_ctx.argc);
    const CONTEXT* c = info->ContextRecord;
    logf("scripts:   RCX %llX RDX %llX R8 %llX R9 %llX RAX %llX RBX %llX RDI %llX RSI %llX", (unsigned long long)c->Rcx,
         (unsigned long long)c->Rdx, (unsigned long long)c->R8, (unsigned long long)c->R9, (unsigned long long)c->Rax,
         (unsigned long long)c->Rbx, (unsigned long long)c->Rdi, (unsigned long long)c->Rsi);
    int shown = 0;
    const auto* sp = reinterpret_cast<const uint64_t*>(c->Rsp);
    for (int i = 0; i < 96 && shown < 14; ++i) {
        uint64_t v;
        SIZE_T got = 0;
        if (!ReadProcessMemory(GetCurrentProcess(), sp + i, &v, 8, &got) || got != 8) break;
        const std::string d = describe_address(v);
        if (d != "?") {
            logf("scripts:   stack[%d] %016llX %s", i, (unsigned long long)v, d.c_str());
            ++shown;
        }
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

bool signed_before(DWORD a, DWORD b) { return static_cast<int32_t>(a - b) < 0; }

void CALLBACK fiber_entry(LPVOID param) {
    auto* s = static_cast<Script*>(param);
    try {
        s->main();
    } catch (const std::exception& e) {
        logf("scripts: script %p ended with an exception: %s", reinterpret_cast<void*>(s->main), e.what());
    } catch (...) {
        logf("scripts: script %p ended with an unknown exception", reinterpret_cast<void*>(s->main));
    }
    s->dead = true;
    // A fiber function must never return (that would end the thread), so hand control back.
    SwitchToFiber(g_main_fiber);
}

void run_scripts() {
    std::vector<Script*> batch;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        for (auto& s : g_scripts) batch.push_back(s.get());
    }
    const DWORD now = GetTickCount();
    for (Script* s : batch) {
        if (s->dead || signed_before(now, s->wake)) continue;
        if (!s->fiber) {
            s->fiber = CreateFiber(kFiberStack, fiber_entry, s);
            if (!s->fiber) {
                logf("scripts: CreateFiber failed (%lu)", GetLastError());
                s->dead = true;
                continue;
            }
        }
        g_current = s;
        g_watched = s;
        g_run_started = GetTickCount() | 1; // never 0
        static int traced = 0;
        if (traced < 40) {
            ++traced;
            logf("scripts: resuming %s (main %p)", module_name(s->module).c_str(), reinterpret_cast<void*>(s->main));
        }
        SwitchToFiber(s->fiber);
        g_run_started = 0;
        g_current = nullptr;
    }
    std::lock_guard<std::mutex> lock(g_lock);
    for (auto it = g_scripts.begin(); it != g_scripts.end();) {
        if ((*it)->dead) {
            if ((*it)->fiber) DeleteFiber((*it)->fiber);
            it = g_scripts.erase(it);
        } else {
            ++it;
        }
    }
}

// Keyboard events are synthesised by polling: the game window procedure is not hooked.
struct KeyState {
    bool down = false;
    DWORD next_repeat = 0;
};
KeyState g_keys[256];

void poll_keyboard() {
    std::vector<KeyboardHandler> handlers;
    {
        std::lock_guard<std::mutex> lock(g_lock);
        handlers = g_keyboard;
    }
    if (handlers.empty()) return;

    DWORD pid = 0;
    GetWindowThreadProcessId(GetForegroundWindow(), &pid);
    const bool focused = pid == GetCurrentProcessId();
    const DWORD now = GetTickCount();
    const bool alt = focused && (GetAsyncKeyState(VK_MENU) & 0x8000);

    for (int vk = 8; vk < 255; ++vk) {
        KeyState& k = g_keys[vk];
        const bool down = focused && (GetAsyncKeyState(vk) & 0x8000);
        const BYTE scan = static_cast<BYTE>(MapVirtualKeyW(vk, MAPVK_VK_TO_VSC));
        const BOOL ext = (vk >= VK_PRIOR && vk <= VK_DOWN) || vk == VK_INSERT || vk == VK_DELETE || vk == VK_RCONTROL ||
                         vk == VK_RMENU || vk == VK_DIVIDE || vk == VK_NUMLOCK;
        bool fire = false, was_down = k.down, up = false;
        if (down && !k.down) {
            fire = true;
            k.next_repeat = now + 500;
        } else if (down && k.down && !signed_before(now, k.next_repeat)) {
            fire = true;
            k.next_repeat = now + 33;
        } else if (!down && k.down) {
            fire = true;
            up = true;
        }
        k.down = down;
        if (!fire) continue;
        for (KeyboardHandler h : handlers) h(static_cast<DWORD>(vk), 1, scan, ext, alt, was_down, up);
    }
}

} // namespace

void register_script(HMODULE module, ScriptMain main) {
    if (!main) return;
    std::lock_guard<std::mutex> lock(g_lock);
    auto s = std::make_unique<Script>();
    s->module = module;
    s->main = main;
    g_scripts.push_back(std::move(s));
    logf("scripts: registered %p (module %p)", reinterpret_cast<void*>(main), static_cast<void*>(module));
}

void unregister(HMODULE module) {
    std::lock_guard<std::mutex> lock(g_lock);
    for (auto& s : g_scripts)
        if (s->module == module) s->dead = true;
}

void unregister(ScriptMain main) {
    std::lock_guard<std::mutex> lock(g_lock);
    for (auto& s : g_scripts)
        if (s->main == main) s->dead = true;
}

bool in_script() { return g_current != nullptr; }

void wait(DWORD ms) {
    if (!g_current || !g_main_fiber) return;
    g_current->wake = GetTickCount() + ms;
    SwitchToFiber(g_main_fiber);
}

void keyboard_register(KeyboardHandler handler) {
    std::lock_guard<std::mutex> lock(g_lock);
    if (std::find(g_keyboard.begin(), g_keyboard.end(), handler) == g_keyboard.end()) g_keyboard.push_back(handler);
}

void keyboard_unregister(KeyboardHandler handler) {
    std::lock_guard<std::mutex> lock(g_lock);
    g_keyboard.erase(std::remove(g_keyboard.begin(), g_keyboard.end(), handler), g_keyboard.end());
}

void native_init(uint64_t hash) {
    std::memset(&t_ctx, 0, sizeof t_ctx);
    t_ctx.ret = t_ctx.args = t_ctx.stack;
    t_hash = hash;
    const uint64_t handler = native_handler(hash);
    t_handler = reinterpret_cast<NativeHandler>(handler);
    if (!handler) {
        static std::unordered_set<uint64_t> reported;
        if (reported.insert(hash).second) logf("scripts: native 0x%llX is not available", (unsigned long long)hash);
    }
}

void native_push(uint64_t value) {
    if (t_ctx.argc < kMaxArgs) t_ctx.stack[t_ctx.argc++] = value;
}

uint64_t* native_call() {
    const uint64_t a0 = t_ctx.stack[0], a1 = t_ctx.stack[1], a2 = t_ctx.stack[2];
    if (t_handler) {
        auto it = g_native_crashes.find(t_hash);
        if (it == g_native_crashes.end() || it->second < kMaxNativeCrashes) {
            if (__builtin_setjmp(t_recover) == 0) {
                t_recovering = true;
                t_handler(&t_ctx);
                if (const uint64_t fn = vector_results_function()) reinterpret_cast<SetVectorResultsFn>(fn)(&t_ctx);
            } else {
                std::memset(t_ctx.stack, 0, sizeof t_ctx.stack); // faulted: report an empty result
            }
            t_recovering = false;
        }
    }
    CallTrace& t = g_trace[g_trace_pos.fetch_add(1) % kTraceSize];
    t = {t_hash, t_ctx.argc, a0, a1, a2, t_ctx.stack[0]};
    return t_ctx.stack;
}

// Scripts must not run while the game is loading: some wait for the end of the loading screen in a
// busy loop, which would block the very thread that has to finish the loading. The game state
// itself has no known address, so the loading-screen native stands in for it.
bool loading_screen_active() {
    constexpr uint64_t kGetIsLoadingScreenActive = 0x10D0A8F259E93EC9ull;
    native_init(kGetIsLoadingScreenActive);
    if (!t_handler) return false; // unknown native: do not hold the scripts back forever
    return (*native_call() & 0xFF) != 0;
}

// Dev aid: log the set of script names whenever it changes, to learn how it tracks the game state.
void trace_script_names() {
    static DWORD last_check = 0;
    static std::string last;
    const DWORD now = GetTickCount();
    if (now - last_check < 1000) return;
    last_check = now;
    std::string joined;
    for (const std::string& n : live_script_names()) joined += n + " ";
    if (joined != last) {
        last = joined;
        logf("scripts: live scripts: %s", joined.substr(0, 200).c_str());
    }
}

bool g_observe_only = false;

void set_observe_only(bool on) { g_observe_only = on; }

void run_frame() {
    if (!g_main_fiber) {
        g_main_fiber = ConvertThreadToFiber(nullptr);
        g_main_thread = GetCurrentThreadId();
        g_main_thread_id = g_main_thread;
        if (!g_main_fiber) {
            logf("scripts: ConvertThreadToFiber failed (%lu), scripts disabled", GetLastError());
            return;
        }
        logf("scripts: scheduler running on thread %lu", g_main_thread);
        if (HANDLE t = CreateThread(nullptr, 0, watchdog, nullptr, 0, nullptr)) CloseHandle(t);
        AddVectoredExceptionHandler(1, exception_logger);
    }
    if (GetCurrentThreadId() != g_main_thread) return; // the frame must always run on the same thread
    trace_script_names();
    static int last_state = -1;
    const int loading = loading_screen_active() ? 1 : 0;
    if (loading != last_state) {
        last_state = loading;
        logf("scripts: loading screen %s, scripts %s", loading ? "active" : "gone", loading ? "paused" : "running");
    }
    // The game is not ready for natives the instant the loading screen disappears (some managers are
    // created a moment later), so hold the scripts back for a short grace period.
    static DWORD ready_since = 0;
    if (loading) ready_since = 0;
    else if (!ready_since) ready_since = GetTickCount() | 1;
    constexpr DWORD kGraceMs = 4000;
    if (loading || g_observe_only || GetTickCount() - ready_since < kGraceMs) return;
    run_scripts();
    poll_keyboard();
}

} // namespace scripts
