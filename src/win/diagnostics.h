#pragma once

#include "core/patterns.h"

struct ScanReport {
    int ok = 0;
    int missing = 0;   // pattern defined but not found
    int ambiguous = 0; // pattern found more than once
    int undefined = 0; // no pattern known for this edition
};

// Edition from the running executable's name (GTA5_Enhanced.exe vs GTA5.exe).
vh::Edition detect_edition();

// Scans the executable sections of the main module for every entry of vh::kPatterns.
// With verbose set, logs one line per pattern.
ScanReport run_diagnostics(vh::Edition edition, bool verbose);

// Absolute address a table entry resolves to in the running process, or 0 when its pattern is
// undefined for the edition, missing or ambiguous.
uintptr_t find_address(const char* name, vh::Edition edition);
