# TODO

State at 2026-10-16: scripts, natives, hash translation and crash recovery work on Legacy
(1.0.3889) and Enhanced (1.0.1158). Everything below is what is still missing or unverified.
"Verified" means seen working on the live game, "inferred" means deduced from code or strings.

## 1. Entity pools (largest gap, blocks several mods)

`worldGetAllVehicles/Peds/Objects/Pickups` return 0 (`src/win/shv_api.cpp`, `vh_worldGetAll`).
InversePower, J10RailroadEngineer, Rain-wash-v, ENT and `getScriptHandleBaseAddress`-based code
that scans the world see no entities.

- Port the enumeration from ScriptHookVDotNetEnhanced `source/core/NativeMemory.cs`:
  `FwScriptGuidPoolTask`, `FwBasePool*`, `RageSysMemPoolAllocator*`, `s_createGuid`
  (handle for an entity address), pool address patterns (vehicle pool, ped/object pools, pickup
  pool). Layouts differ between editions: all `fwScriptGuid` fields are +0x08 in Enhanced.
- Enhanced stores some pool pointers obfuscated (rol/xor/not sequence computed from three
  globals, see lines ~459-555 of `NativeMemory.cs`); reproduce it in C++ and re-run it when the
  pool is not yet initialised.
- Add the patterns to `src/core/patterns.h`, check them with `tools/scan_image` on a dump made
  with `VinewoodHook.dump`, then implement the four exports and return real script handles.
- `GET_ALL_VEHICLES` (hash 0x9B8E1BF04B51F2E8) and `GET_PED_NEARBY_*` exist as natives but are not
  a substitute (limited or nearby only). GTA V has no `FIND_FIRST_*` natives.
- Check the fwScriptGuid pool is not full (`IsFull`): the original ScriptHookV raises its size.

## 2. Script globals on Enhanced

`getGlobalPtr` uses the real table on Legacy (pattern `scriptGlobals`, block = `(id>>18)&0x3F`,
slot = `id & 0x3FFFF`) and a zeroed 128 MB buffer elsewhere, so Enhanced scripts read zeros and
writes go nowhere (ENT and SHVDN script globals are affected).

- Find the Enhanced accessor. The Legacy one is `4C 8D 05 ? ? ? ? 4D 8B 08 4D 85 C9 74 11`; no
  equivalent was found in the first Enhanced dump. The loader that fills the blocks is around
  Enhanced RVA 0x91BDF7 (`shr 0x12`, `and 0x3FFFF`, array at 0x3ED17A8) and is a starting point.
- Make `vh_getGlobalPtr` log once when the fallback is used (it does) and never hand out the
  fallback for writes that matter without a warning.

## 3. SetVectorResults on Enhanced

Located on Legacy (RVA 0x167578C, pattern in `patterns.h`). Not located on Enhanced, so vector
outputs through pointer arguments (e.g. `GET_MODEL_DIMENSIONS`) may come back wrong there.
The Legacy function copies `dataCount` vectors from the context at +0x40 (16 bytes each) to the
pointers at +0x20. Find the Enhanced counterpart the same way (look at what the interpreter does
after calling a native handler).

## 4. Game state instead of the loading-screen heuristic

Scripts are held back while `GET_IS_LOADING_SCREEN_ACTIVE` is true and for 4 s after
(`src/win/scripts.cpp`, `run_frame`). The real ScriptHookV waits for the game state "Playing"
(OpenVHook's `gameState`: Playing=0, Intro=1, LicenseShit=3, MainMenu=5, LoadingSP_MP=6).

- The OpenVHook pattern `83 3D ? ? ? ? ? 8A D9 74 0A` does not match either edition. Three Legacy
  candidates were read live and none changed with the state (RVAs 0x1D1EBE8, 0x2602240,
  0x291B958), so they are not it.
- Idea: dump the module twice (landing page vs in game, `VinewoodHook.dump`) and diff writable
  ints; or follow the code that decides whether `scrThread::Tick` runs scripts.
- Remove the fixed 4 s grace period once a real state is available.

## 5. Things that are inferred, not verified

- Enhanced script name offset +0x154 in the thread object (from strings; Legacy +0xD4 comes from
  the game's own debug print). Used to pick the anchor thread `main_persistent`.
- Tick is vtable slot 3 on both editions (same scheduler loop in both exes, seen in code).
- `getGameVersion` values (Legacy 105, Enhanced 1010) are guesses. Real values: Legacy
  1.0.3889 is above 101 (1.0.3788); Enhanced 1.0.1158.13 follows 1009 (2.0.1013.33).
  ScriptHookV.log of the original says `VER_EN_1_0_1158_13`. Plugins use `>= 1000` for Enhanced.
- `GET_REAL_WORLD_TIME` returned 29..31, assumed to be seconds; not checked against a clock.
- The first `GET_CLOCK_HOURS/MINUTES` reads (12:00) look like an uninitialised clock; not
  investigated.

## 6. Input

Keyboard events are synthesised by polling `GetAsyncKeyState` once per frame
(`poll_keyboard`): untested with real keys, and it misses very short taps between frames. Better:
hook the game window procedure (`SetWindowLongPtr`, as OpenVHook does) and forward
`WM_KEYDOWN/UP/SYSKEY*` with the real repeat count, scan code and extended flag.
Test with a mod that uses a key (InteriorsV toggles blips on F3, vehicle cannon).

## 7. Rendering API

`presentCallbackRegister/Unregister`, `createTexture`, `drawTexture` are stubs (ENT uses the
texture ones, some trainers use present callbacks). Needs a D3D11/D3D12 present hook (Enhanced
uses DX12 by default under Proton? check) to call the callbacks and draw textures. Large item.

## 8. Script runtime hardening

- A script that never calls `scriptWait` still freezes the game (nothing can preempt it); the
  watchdog only names it. Option: a per-frame time budget is impossible with fibers, but a
  watchdog could kill the script via a flag checked in `nativeCall`/`scriptWait`.
- Long initialisations (ScriptHookVDotNet, InteriorsV, ENT) block the game for a few seconds at
  start; could be spread by starting scripts one per frame.
- `SuspendThread` on the game thread is refused under Wine (error 5), so the watchdog cannot
  report where a thread is stuck.
- Fiber stack size is 1 MB (`kFiberStack`); SHVDN and big mods may need more.
- Scripts can call `scriptUnregister` from inside themselves; the fiber is deleted only after it
  yields. Check mods that unregister on DLL unload.
- Exceptions inside natives are caught only for access violations and are recovered with a
  non-unwinding `__builtin_longjmp` (game state may be left half-updated).
- `getGameVersionInfo` is not exported (needs the `GameVersionInfo` struct layout).

## 9. Maintenance and tooling

- No git repository yet. Create one before the next big change.
- Pattern table: `scrThreadId`, `scrThreadCount` and `gameState` are old OpenVHook entries that do
  not match (reported MISSING on Legacy); `nativeRegistrationTable` has a Legacy pattern but the
  table is also found by structure on both editions. Remove or replace dead entries.
- Make `tools/scan_image` print the new entries per edition in CI-like form and add a test that
  runs it on dumps when present.
- Add native tests for `scripts` logic that is portable (key repeat timing, wait scheduling) by
  moving it out of the Windows-only files.
- Add a self test that calls more natives, including vector and string arguments.
- Document how to produce an image dump (`VinewoodHook.dump`) and diff two builds.

## 10. Before sharing the project (not planned, personal use for now)

- `data/native_hashes.txt` comes from the ScriptHookV.dll installed on the author's PC (permission
  was reported by the user for personal use). Re-check the rights, or derive it independently:
  the two editions share about 6699 current hashes, so a Legacy vs Enhanced comparison plus the
  public databases (`natives.json`, `natives_gen9.json` from alloc8or/gta5-nativedb-data) is the
  clean route. Do not redistribute the current file without that.
- The Legacy and Enhanced patterns taken from OpenVHook and ScriptHookVDotNet(Enhanced) carry
  their licenses.
- Strip the dev-only code paths (observe/probes/dump markers) or document them.

## Operational notes

- Launch options in Steam must contain `dinput8` in `WINEDLLOVERRIDES` (Enhanced:
  `"xinput1_4,dinput8,ScriptHookV=n,b"`, Legacy: `"dinput8,ScriptHookV=n,b"`).
- Never remove `xinput1_4.dll` from the Enhanced folder: it loads OpenRPF.asi and without it the
  game stops with ERR_STR_MISSING.
- The original `ScriptHookV.dll` of Enhanced is kept as `ScriptHookV.dll.original`; restoring it
  and deleting our `dinput8.dll` undoes the install.
- Games can be started with `steam steam://rungameid/271590` (Legacy) and `.../3240220`
  (Enhanced); close them with `pkill -f '[G]TA5.exe'` / `'[G]TA5_Enhanced.exe'`.
