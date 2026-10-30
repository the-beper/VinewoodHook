#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <vector>

namespace vh {

// Native registration table: 256 buckets indexed by (hash & 0xFF), each a linked list of
// registration nodes holding up to 7 (hash, handler) pairs. Pointers, counts and hashes are
// XOR-obfuscated with their own address; the scheme is taken from OpenVHook (Legacy) and
// has not been confirmed for Enhanced yet, so the structural check below doubles as the
// confirmation: a layout is only accepted when every decoded hash lands in its own bucket.
constexpr size_t kBuckets = 256;
constexpr size_t kRegBytes = 0xD0; // enough for the highest hashes offset + 7 * 16

// Reads n bytes of target memory at addr; false when unreadable.
using ReadFn = std::function<bool(uint64_t addr, void* out, size_t n)>;
// True when addr points into executable memory.
using CodeFn = std::function<bool(uint64_t addr)>;

struct NativeEntry {
    uint64_t hash;
    uint64_t handler;
};

// Offset of the hashes array inside a node (pack(4) in OpenVHook gives 0x54).
inline constexpr size_t kRegLayouts[] = {0x54, 0x58};

// Decodes one node (buf holds kRegBytes read from addr). False when the count is out of range.
bool decode_registration(uint64_t addr, const uint8_t* buf, size_t hashes_off, std::vector<NativeEntry>& out,
                         uint64_t& next);

struct TableScore {
    size_t nonnull = 0; // buckets with a non-null head pointer
    size_t good = 0;    // heads that decode and pass every check
    int layout = -1;    // index into kRegLayouts of the best layout, -1 if none
    bool ok() const { return layout >= 0 && nonnull >= 128 && good * 10 >= nonnull * 9; }
};

// Scores the 256-pointer array at table_addr.
TableScore score_table(const ReadFn& read, const CodeFn& is_code, uint64_t table_addr);

// Every (hash, handler) reachable from the table, following the chains.
std::vector<NativeEntry> read_all_natives(const ReadFn& read, uint64_t table_addr, int layout);

} // namespace vh
