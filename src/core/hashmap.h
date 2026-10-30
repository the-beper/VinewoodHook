#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace vh {

// Translates the native hashes used by public databases and scripts into the hashes the
// running game registers. Text format: one "PUBLIC CURRENT" pair of 64-bit hex numbers per line.
class HashMap {
public:
    // Adds every well-formed line of text; returns how many pairs were read. Bad lines are skipped.
    size_t load_text(std::string_view text);

    // The registered hash for a public one. Unknown hashes are returned unchanged, because many
    // natives have the same hash everywhere.
    uint64_t translate(uint64_t public_hash) const;

    size_t size() const { return map_.size(); }

private:
    std::unordered_map<uint64_t, uint64_t> map_;
};

} // namespace vh
