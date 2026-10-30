// Applies the pattern table to a raw in-memory image dump (file offset == RVA).
// Usage: scan_image IMAGE.bin legacy|enhanced
#include "core/pattern.h"
#include "core/patterns.h"

#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char** argv) {
    if (argc != 3) {
        std::fprintf(stderr, "usage: %s IMAGE.bin legacy|enhanced\n", argv[0]);
        return 2;
    }
    std::ifstream f(argv[1], std::ios::binary);
    std::vector<uint8_t> img((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    if (img.empty()) {
        std::fprintf(stderr, "cannot read %s\n", argv[1]);
        return 2;
    }
    const bool enhanced = std::strcmp(argv[2], "enhanced") == 0;
    int bad = 0;
    for (const auto& d : vh::kPatterns) {
        const vh::Locator& l = enhanced ? d.enhanced : d.legacy;
        if (!l.pattern) {
            std::printf("%-24s no pattern\n", d.name);
            continue;
        }
        auto p = vh::parse_pattern(l.pattern);
        auto hits = p ? vh::scan(img.data(), img.size(), *p, 2) : std::vector<size_t>{};
        if (hits.size() != 1) {
            std::printf("%-24s %s\n", d.name, hits.empty() ? "MISSING" : "AMBIGUOUS");
            ++bad;
            continue;
        }
        if (l.instr_len == 0) {
            std::printf("%-24s OK  match 0x%zX\n", d.name, hits[0]);
        } else if (auto t = vh::resolve_rel32(img.data(), img.size(), hits[0], l.disp_off, l.instr_len)) {
            std::printf("%-24s OK  match 0x%zX -> 0x%llX\n", d.name, hits[0], (unsigned long long)*t);
        } else {
            std::printf("%-24s OK  match 0x%zX (rel32 out of range)\n", d.name, hits[0]);
        }
    }
    return bad ? 1 : 0;
}
