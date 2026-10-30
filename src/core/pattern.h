#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace vh {

// A byte pattern in IDA style, e.g. "48 8B 05 ? ? ? ? C3". "?" and "??" are wildcards.
struct Pattern {
    std::vector<uint8_t> bytes;
    std::vector<uint8_t> wild; // 1 where the byte is a wildcard
};

// Returns nullopt on empty input or any malformed token.
std::optional<Pattern> parse_pattern(std::string_view text);

// Offsets of up to max_matches matches of p inside data[0, size).
std::vector<size_t> scan(const uint8_t* data, size_t size, const Pattern& p, size_t max_matches);

// Resolves a RIP-relative rel32 operand. disp_off is the offset of the 4-byte
// displacement from the match start, instr_len the length of the whole instruction
// counted from the match start. Returns the target offset relative to data (it may
// fall outside the buffer), or nullopt if the displacement itself is out of bounds.
std::optional<int64_t> resolve_rel32(const uint8_t* data, size_t size, size_t match, size_t disp_off,
                                     size_t instr_len);

} // namespace vh
