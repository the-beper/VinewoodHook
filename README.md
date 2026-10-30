# VinewoodHook

Script hook for GTA V (Legacy and Enhanced, single player, personal use). It finds game
internals by pattern scanning and structure checks instead of fixed offsets, so a game update
only requires fixing the patterns that actually changed, and it exposes the ScriptHookV C++ API
so existing `.asi` plugins (including ScriptHookVDotNet) run on top of it unchanged.

## Status

Working on both editions (verified on a live game under Proton):

- script thread scan, native table found by structure, public-hash to current-hash translation
  (`data/native_hashes.txt`, 6735 entries);
- a hook on the script thread `Tick` runs registered scripts once per frame on fibers, with the
  active-script context set, `scriptWait`, native calls and keyboard events (polled);
- natives that fault are caught (the call returns 0, three faults disable that native) and a
  watchdog names any script that never yields.

Not implemented yet (the API entry points exist and return empty results, each logs once):
`worldGetAllVehicles/Peds/Objects/Pickups`, `getGlobalPtr` on Enhanced (Legacy has the real
table), present callbacks, `createTexture/drawTexture`. Mods that depend on them load but do not
see entities. `getGameVersion` returns a made-up but plausible index (Legacy 105, Enhanced 1010).

## Layout

- `src/core/` portable code: pattern parser and scanner, the pattern table (`patterns.h`), native
  table decoding, hash map. Tested natively.
- `src/win/` Windows code: `ScriptHookV.dll` (hook, script runtime, SHV API exports) and the
  `dinput8.dll` proxy that loads it.
- `data/native_hashes.txt` public hash to current hash map.
- `tools/scan_image.cpp` applies the pattern table to a memory image of the game.

## Build

Core and tests (host compiler):

    cmake -S . -B build -G Ninja && cmake --build build && ctest --test-dir build

DLLs (cross-compile, needs mingw-w64, e.g. Fedora `mingw64-gcc-c++`):

    cmake -S . -B build-win -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64.cmake
    cmake --build build-win

Outputs `build-win/ScriptHookV.dll` and `build-win/dinput8.dll`.

## Install (Proton)

Next to the game executable put `ScriptHookV.dll`, `dinput8.dll` and a copy of
`data/native_hashes.txt` named `VinewoodHook.hashes.txt`. Launch options must include
`dinput8` in the DLL overrides, e.g. `WINEDLLOVERRIDES="dinput8,ScriptHookV=n,b" %command%`.
`.asi` files are loaded by an existing ASI loader; with none, create an empty
`VinewoodHook.load_asi` and the proxy loads them. To go back to the original ScriptHookV, restore
its `ScriptHookV.dll` and remove `dinput8.dll`.

Marker files next to the DLL (all optional): `VinewoodHook.load_asi`, `VinewoodHook.probes.txt`
(runs the internal self test with the natives listed in it), `VinewoodHook.observe` (log script
names, run nothing), `VinewoodHook.dump` (write `VinewoodHook.image.bin`, the decrypted module
image, for analysis). Everything is logged to `VinewoodHook.log`.

## Patterns

`src/core/patterns.h` holds one entry per game address with a pattern per edition. Entries are
checked against decrypted in-memory images (`tools/scan_image`) and live. The log shows each as
OK, MISSING or AMBIGUOUS, which is the starting point for fixing a broken update. The game code is
encrypted at rest, so images must be dumped from inside the process (`VinewoodHook.dump`).

## Legal

Single player only, with BattlEye disabled. Prior art studied: OpenVHook, ScriptHookVDotNetEnhanced,
GameplayFixesV. Their licenses apply to anything copied from them.
