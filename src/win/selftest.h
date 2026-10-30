#pragma once

// Internal script used to check the runtime on a live game: every second it calls the natives
// listed in VinewoodHook.probes.txt ("NAME 0xHASH" per line, zero arguments, 32-bit result)
// plus SQRT(16), logs the values, and stops after five rounds.
void selftest_main();
