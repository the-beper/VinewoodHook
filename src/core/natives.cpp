#include "core/natives.h"

#include <cstring>

namespace vh {

namespace {

constexpr size_t kChainLimit = 4096; // guards against cycles in a corrupt table

uint64_t read_u64(const uint8_t* p) {
    uint64_t v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

uint32_t read_u32(const uint8_t* p) {
    uint32_t v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

uint64_t unmask(uint64_t value, uint64_t key_source) {
    const uint32_t key = static_cast<uint32_t>(key_source);
    return value ^ (static_cast<uint64_t>(key) << 32) ^ key;
}

} // namespace

bool decode_registration(uint64_t addr, const uint8_t* buf, size_t hashes_off, std::vector<NativeEntry>& out,
                         uint64_t& next) {
    const uint32_t count = static_cast<uint32_t>(addr + 0x48) ^ read_u32(buf + 0x48) ^ read_u32(buf + 0x4C);
    if (count < 1 || count > 7) return false;
    next = unmask(read_u64(buf), addr ^ read_u64(buf + 8));
    out.clear();
    for (uint32_t i = 0; i < count; ++i) {
        const size_t slot = hashes_off + 16 * i;
        const uint64_t hash = unmask(read_u64(buf + slot), (addr + slot) ^ read_u64(buf + slot + 8));
        out.push_back({hash, read_u64(buf + 0x10 + 8 * i)});
    }
    return true;
}

TableScore score_table(const ReadFn& read, const CodeFn& is_code, uint64_t table_addr) {
    uint64_t heads[kBuckets];
    if (!read(table_addr, heads, sizeof heads)) return {};

    TableScore best;
    for (size_t l = 0; l < sizeof kRegLayouts / sizeof kRegLayouts[0]; ++l) {
        TableScore s;
        for (size_t b = 0; b < kBuckets; ++b) {
            if (!heads[b]) continue;
            ++s.nonnull;
            if (s.nonnull > 16 && s.good == 0) break; // clearly not a table, skip the rest
            uint8_t buf[kRegBytes];
            std::vector<NativeEntry> entries;
            uint64_t next;
            if (!read(heads[b], buf, sizeof buf) || !decode_registration(heads[b], buf, kRegLayouts[l], entries, next))
                continue;
            bool good = true;
            for (const auto& e : entries) good = good && (e.hash & 0xFF) == b && is_code(e.handler);
            if (good) ++s.good;
        }
        s.layout = static_cast<int>(l);
        if (best.layout < 0 || s.good > best.good) best = s;
    }
    if (best.good == 0) best.layout = -1;
    return best;
}

std::vector<NativeEntry> read_all_natives(const ReadFn& read, uint64_t table_addr, int layout) {
    std::vector<NativeEntry> all;
    if (layout < 0) return all;
    uint64_t heads[kBuckets];
    if (!read(table_addr, heads, sizeof heads)) return all;
    for (size_t b = 0; b < kBuckets; ++b) {
        uint64_t node = heads[b];
        for (size_t n = 0; node && n < kChainLimit; ++n) {
            uint8_t buf[kRegBytes];
            std::vector<NativeEntry> entries;
            uint64_t next;
            if (!read(node, buf, sizeof buf) || !decode_registration(node, buf, kRegLayouts[layout], entries, next))
                break;
            all.insert(all.end(), entries.begin(), entries.end());
            node = next;
        }
    }
    return all;
}

} // namespace vh
