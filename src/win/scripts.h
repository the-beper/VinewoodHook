#pragma once

#include <windows.h>

#include <cstdint>

// Script runtime behind the ScriptHookV-compatible API: every registered script main runs on its
// own fiber, scheduled once per frame from the game thread. scriptWait() yields back to it.
namespace scripts {

using ScriptMain = void (*)();
using KeyboardHandler = void (*)(DWORD key, WORD repeats, BYTE scan_code, BOOL is_extended, BOOL is_with_alt,
                                 BOOL was_down_before, BOOL is_up_now);

void register_script(HMODULE module, ScriptMain main);
void unregister(HMODULE module);
void unregister(ScriptMain main);
void wait(DWORD ms);
bool in_script();

void keyboard_register(KeyboardHandler handler);
void keyboard_unregister(KeyboardHandler handler);

// Native invocation, one context per OS thread (fibers of a thread share it, as in ScriptHookV).
void native_init(uint64_t hash);
void native_push(uint64_t value);
uint64_t* native_call();

// Dev aid: only log how the set of game scripts evolves, run none of the registered scripts.
void set_observe_only(bool on);

// Called once per frame by the hook, on the game thread.
void run_frame();

} // namespace scripts
