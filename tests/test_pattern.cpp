#include "core/pattern.h"
#include "core/patterns.h"

#include <cstdio>
#include <cstring>

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

int main() {
    using namespace vh;

    // parsing
    auto p = parse_pattern("48 8b 05 ? ?? ? ? C3");
    CHECK(p && p->bytes.size() == 8);
    CHECK(p && p->bytes[0] == 0x48 && p->bytes[1] == 0x8B && p->bytes[7] == 0xC3);
    CHECK(p && !p->wild[0] && p->wild[3] && p->wild[4] && p->wild[6]);
    CHECK(!parse_pattern(""));
    CHECK(!parse_pattern("   "));
    CHECK(!parse_pattern("48 G1"));
    CHECK(!parse_pattern("48 8"));
    CHECK(!parse_pattern("488B"));

    // scanning
    const uint8_t buf[] = {0x00, 0x48, 0x8B, 0x05, 0x10, 0x00, 0x00, 0x00, 0xC3, 0x48, 0x8B, 0x05, 0x20, 0x00, 0x00, 0x00, 0xC3};
    auto m = scan(buf, sizeof buf, *p, 10);
    CHECK(m.size() == 2 && m[0] == 1 && m[1] == 9);
    CHECK(scan(buf, sizeof buf, *p, 1).size() == 1);
    CHECK(scan(buf, sizeof buf, *p, 0).empty());
    auto none = parse_pattern("DE AD BE EF");
    CHECK(scan(buf, sizeof buf, *none, 10).empty());
    CHECK(scan(buf, 3, *p, 10).empty()); // buffer shorter than pattern

    // pattern touching the very end of the buffer
    auto tail = parse_pattern("20 00 00 00 C3");
    auto tm = scan(buf, sizeof buf, *tail, 10);
    CHECK(tm.size() == 1 && tm[0] == 12);

    // rel32 resolution: instruction at 1 is "48 8B 05 <rel32>" (7 bytes), rel = 0x10
    auto t = resolve_rel32(buf, sizeof buf, 1, 3, 7);
    CHECK(t && *t == 1 + 7 + 0x10);
    CHECK(!resolve_rel32(buf, sizeof buf, 14, 3, 7)); // displacement out of bounds

    // every pattern in the table must be well formed
    for (const auto& d : kPatterns) {
        for (const Locator* l : {&d.legacy, &d.enhanced}) {
            if (!l->pattern) continue;
            CHECK(parse_pattern(l->pattern));
            CHECK(l->instr_len == 0 || l->disp_off + 4 <= l->instr_len);
        }
    }

    if (g_failures == 0) std::puts("all tests passed");
    return g_failures == 0 ? 0 : 1;
}
