#include "win/diagnostics.h"

#include "core/pattern.h"
#include "win/log.h"

#include <cwctype>
#include <string>
#include <string_view>
#include <vector>

using namespace vh;

Edition detect_edition() {
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring name(path, n < MAX_PATH ? n : 0);
    name = name.substr(name.find_last_of(L"\\/") + 1);
    for (auto& c : name) c = static_cast<wchar_t>(std::towlower(c));
    return name == L"gta5_enhanced.exe" ? Edition::Enhanced : Edition::Legacy;
}

namespace {

struct Hit {
    const uint8_t* data;
    size_t size;
    size_t offset;
    uint32_t section_rva;
};

// Collects up to two matches (enough to tell "unique" from "ambiguous") across all
// executable sections.
std::vector<Hit> find_hits(const Pattern& pattern) {
    std::vector<Hit> hits;
    const auto* base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections && hits.size() < 2; ++i) {
        const DWORD ch = sec[i].Characteristics;
        if (!(ch & IMAGE_SCN_MEM_EXECUTE) || (ch & IMAGE_SCN_MEM_DISCARDABLE)) continue;
        const uint8_t* data = base + sec[i].VirtualAddress;
        const size_t size = sec[i].Misc.VirtualSize;
        for (size_t off : scan(data, size, pattern, 2 - hits.size()))
            hits.push_back({data, size, off, sec[i].VirtualAddress});
    }
    return hits;
}

} // namespace

ScanReport run_diagnostics(Edition edition, bool verbose) {
    ScanReport r;
    for (const auto& def : kPatterns) {
        const Locator& loc = edition == Edition::Enhanced ? def.enhanced : def.legacy;
        const char* text = loc.pattern;
        if (!text) {
            ++r.undefined;
            if (verbose) logf("%-24s no pattern for this edition", def.name);
            continue;
        }
        auto pattern = parse_pattern(text);
        if (!pattern) {
            ++r.missing;
            if (verbose) logf("%-24s INVALID PATTERN \"%s\"", def.name, text);
            continue;
        }
        std::vector<Hit> hits = find_hits(*pattern);
        if (hits.empty()) {
            ++r.missing;
            if (verbose) logf("%-24s MISSING   \"%s\"", def.name, text);
        } else if (hits.size() > 1) {
            ++r.ambiguous;
            if (verbose) logf("%-24s AMBIGUOUS \"%s\"", def.name, text);
        } else {
            ++r.ok;
            if (!verbose) continue;
            const Hit& h = hits[0];
            const uint64_t match_rva = h.section_rva + h.offset;
            if (loc.instr_len == 0) {
                logf("%-24s OK  match rva 0x%llX", def.name, (unsigned long long)match_rva);
            } else if (auto t = resolve_rel32(h.data, h.size, h.offset, loc.disp_off, loc.instr_len)) {
                logf("%-24s OK  match rva 0x%llX -> target rva 0x%llX", def.name, (unsigned long long)match_rva,
                     (unsigned long long)(h.section_rva + *t));
            } else {
                logf("%-24s OK  match rva 0x%llX (rel32 out of section)", def.name, (unsigned long long)match_rva);
            }
        }
    }
    return r;
}

uintptr_t find_address(const char* name, Edition edition) {
    for (const auto& def : kPatterns) {
        if (std::string_view(def.name) != name) continue;
        const Locator& loc = edition == Edition::Enhanced ? def.enhanced : def.legacy;
        if (!loc.pattern) return 0;
        auto pattern = parse_pattern(loc.pattern);
        if (!pattern) return 0;
        std::vector<Hit> hits = find_hits(*pattern);
        if (hits.size() != 1) return 0;
        const Hit& h = hits[0];
        const auto base = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
        if (loc.instr_len == 0) return base + h.section_rva + h.offset;
        auto t = resolve_rel32(h.data, h.size, h.offset, loc.disp_off, loc.instr_len);
        return t ? base + h.section_rva + static_cast<uintptr_t>(*t) : 0;
    }
    return 0;
}
