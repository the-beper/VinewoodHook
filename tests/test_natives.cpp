#include "core/natives.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>

static int g_failures = 0;
#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

using namespace vh;

static uint64_t mask(uint64_t value, uint64_t key_source) {
    const uint32_t key = static_cast<uint32_t>(key_source);
    return value ^ (static_cast<uint64_t>(key) << 32) ^ key;
}

// Builds a node in real memory using the same obfuscation the game uses (XOR is its own inverse).
static uint8_t* make_node(size_t hashes_off, const std::vector<NativeEntry>& entries, const uint8_t* next) {
    auto* n = static_cast<uint8_t*>(std::calloc(1, kRegBytes));
    const uint64_t addr = reinterpret_cast<uint64_t>(n);
    auto put64 = [&](size_t off, uint64_t v) { std::memcpy(n + off, &v, 8); };
    auto put32 = [&](size_t off, uint32_t v) { std::memcpy(n + off, &v, 4); };

    const uint64_t next_key = 0x1234567800ABCDEFull;
    put64(8, next_key);
    put64(0, mask(reinterpret_cast<uint64_t>(next), addr ^ next_key));
    put32(0x48, 0xDEADBEEF);
    put32(0x4C, static_cast<uint32_t>(addr + 0x48) ^ 0xDEADBEEF ^ static_cast<uint32_t>(entries.size()));
    for (size_t i = 0; i < entries.size(); ++i) {
        put64(0x10 + 8 * i, entries[i].handler);
        const size_t slot = hashes_off + 16 * i;
        const uint64_t key2 = 0x0F0F0F0F11223344ull + i;
        put64(slot + 8, key2);
        put64(slot, mask(entries[i].hash, (addr + slot) ^ key2));
    }
    return n;
}

int main() {
    const ReadFn read = [](uint64_t a, void* out, size_t n) {
        std::memcpy(out, reinterpret_cast<void*>(a), n);
        return true;
    };
    const CodeFn any_code = [](uint64_t a) { return a >= 0x1000; };

    for (int layout = 0; layout < 2; ++layout) {
        const size_t off = kRegLayouts[layout];
        static uint64_t table[kBuckets];
        std::memset(table, 0, sizeof table);
        size_t expected = 0;
        for (size_t b = 0; b < kBuckets; ++b) {
            // two nodes per bucket: the second one is reached through the obfuscated next pointer
            std::vector<NativeEntry> e2{{0x9CD27B0045628400ull | b, 0x140001000}};
            uint8_t* tail = make_node(off, e2, nullptr);
            std::vector<NativeEntry> e1;
            for (uint64_t i = 0; i < 1 + b % 7; ++i)
                e1.push_back({(0x1111000000000000ull + (i << 8)) | b, 0x140002000 + i});
            table[b] = reinterpret_cast<uint64_t>(make_node(off, e1, tail));
            expected += e1.size() + e2.size();
        }
        TableScore s = score_table(read, any_code, reinterpret_cast<uint64_t>(table));
        CHECK(s.ok());
        CHECK(s.layout == layout);
        CHECK(s.nonnull == kBuckets && s.good == kBuckets);
        auto all = read_all_natives(read, reinterpret_cast<uint64_t>(table), s.layout);
        CHECK(all.size() == expected);
        bool found = false;
        for (const auto& e : all) found = found || e.hash == 0x9CD27B0045628463ull;
        CHECK(found);
    }

    // random data must not look like a table
    static uint64_t junk[kBuckets];
    for (auto& v : junk) v = reinterpret_cast<uint64_t>(&junk) + 8 * (std::rand() % 200);
    CHECK(!score_table(read, any_code, reinterpret_cast<uint64_t>(junk)).ok());

    if (g_failures == 0) std::puts("all tests passed");
    return g_failures ? 1 : 0;
}
