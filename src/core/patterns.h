#pragma once

#include <cstddef>

namespace vh {

enum class Edition { Legacy, Enhanced };

// How to find one address in one edition. A null pattern means "not known yet".
// instr_len == 0 means the match itself is the result; otherwise the result is the
// RIP-relative target of the rel32 at disp_off (instr_len counted from the match start).
struct Locator {
    const char* pattern;
    size_t disp_off;
    size_t instr_len;
};

struct PatternDef {
    const char* name;
    Locator legacy;
    Locator enhanced;
};

inline constexpr Locator kUnknown{nullptr, 0, 0};

// Legacy entries come from OpenVHook (2017-2021) and are UNVERIFIED against current
// builds, except scrThreadCollection which was re-derived from a decrypted in-memory image of
// GTA5.exe (2026-10-16, uniqueness and target checked; the scheduler loop that calls
// vtable[3] with the timeslice). Enhanced entries were checked against a decrypted in-memory dump of
// GTA5_Enhanced.exe (2026-10-16): each matched exactly once and resolved to the
// expected global. What the globals mean is inferred from the code around them.
inline constexpr PatternDef kPatterns[] = {
    {"scrThreadCollection",
     {"48 8B 05 ? ? ? ? 8B CF 48 8B 0C C8 39 59 68 75 20 44 39 71 08 74 1A 48 8B 01 8B D5 FF 50 18", 3, 7},
     {"4C 8B 15 ? ? ? ? 66 41 83 F8 01 75 23", 3, 7}},
    // The running script thread lives in a global and in a per-thread block reached through a TLS
    // index. The block offsets (Legacy 0x2A48/0x2A50, Enhanced 0x7A0/0x7A8) are part of the
    // patterns on purpose: if a build moves them the pattern stops matching instead of silently
    // writing to the wrong field.
    {"activeThread",
     {"48 8B 1D ? ? ? ? 8B 35 ? ? ? ? 48 8B 14 F0 41 BF 48 2A 00 00 41 BE 50 2A 00 00", 3, 7},
     {"4C 8B 3D ? ? ? ? 48 89 B0 A0 07 00 00 48 89 35 ? ? ? ? C6 80 A8 07 00 00 01", 3, 7}},
    {"activeThreadTlsIndex",
     {"8B 35 ? ? ? ? 48 8B 14 F0 41 BF 48 2A 00 00 41 BE 50 2A 00 00 4A 8B 3C 3A", 2, 6},
     {"8B 05 ? ? ? ? 65 48 8B 0C 25 58 00 00 00 48 8B 04 C1 4C 8B A0 A0 07 00 00", 2, 6}},
    // scrNativeCallContext::SetVectorResults, flushes vector outputs back after a native ran.
    {"setVectorResults", {"83 79 18 00 48 8B D1 74 4A FF 4A 18 48 63 4A 18", 0, 0}, kUnknown},
    // Script handle -> entity pointer. Patterns taken from ScriptHookVDotNet(Enhanced); the match is
    // the instruction before a call to the resolver.
    {"getScriptEntity", {"85 ED 74 0F 8B CD E8 ? ? ? ? 48 8B F8 48 85 C0 74 2E", 7, 11},
     {"41 8B 4C 1C ? E8", 6, 10}},
    // Table of script global blocks, indexed by (id >> 18) & 0x3F. Legacy only so far.
    {"scriptGlobals", {"4C 8D 05 ? ? ? ? 4D 8B 08 4D 85 C9 74 11", 3, 7}, kUnknown},
    {"scrThreadId", {"89 15 ? ? ? ? 48 8B 0C D8", 2, 6}, kUnknown},
    {"scrThreadCount", {"FF 0D ? ? ? ? 48 8B F9", 2, 6}, kUnknown},
    {"nativeRegistrationTable", {"76 32 48 8B 53 40", 9, 13}, kUnknown},
    {"gameState", {"83 3D ? ? ? ? ? 8A D9 74 0A", 2, 7}, kUnknown},
};

} // namespace vh
