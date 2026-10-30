#pragma once

#include "core/patterns.h"

#include <cstdint>
#include <string>
#include <vector>

// Read-only look at the running game: script thread collection, vtable and the native
// registration table. Everything it finds goes to the log. Safe to call repeatedly.
// Returns true once the native table has been located.
bool explore_game(vh::Edition edition);

// Handler registered for a native, given the hash scripts use (translated through the hash
// map first). 0 when the native does not exist or the table has not been found yet.
uint64_t native_handler(uint64_t public_hash);

// Address of scrNativeCallContext::SetVectorResults, 0 when unknown for this edition.
uint64_t vector_results_function();

// Game functions and tables found by pattern, 0 when unknown for this edition:
// script handle -> entity pointer (uint64_t(*)(int)), and the table of script global blocks.
uint64_t script_entity_function();
uint64_t script_globals_table();

// Names of the script threads that currently exist (id != 0), read straight from the thread objects.
// Name offsets: Legacy +0xD4 (printed by the game's own debug dump), Enhanced +0x154 (inferred from
// the strings inside a thread object, not confirmed against code).
std::vector<std::string> live_script_names();

// Patches the Tick slot of the script thread vtable so that on_frame runs once per frame, on the
// game's own thread, right after a script thread ticked. Needs explore_game() to have succeeded.
using FrameFn = void (*)();
bool install_frame_hook(vh::Edition edition, FrameFn on_frame);

// Dev tool (VinewoodHook.dump marker): writes the in-memory image of the main module to
// VinewoodHook.image.bin next to the DLL. Done from inside the process because the code is
// only decrypted there; an external dump can miss pages that are still encrypted.
void dump_module_image();
