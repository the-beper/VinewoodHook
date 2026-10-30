#include "core/pattern.h"

#include <cstring>

namespace vh {

static int hex_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::optional<Pattern> parse_pattern(std::string_view text) {
    Pattern p;
    size_t i = 0;
    while (i < text.size()) {
        if (text[i] == ' ' || text[i] == '\t') {
            ++i;
            continue;
        }
        size_t j = i;
        while (j < text.size() && text[j] != ' ' && text[j] != '\t') ++j;
        std::string_view tok = text.substr(i, j - i);
        if (tok == "?" || tok == "??") {
            p.bytes.push_back(0);
            p.wild.push_back(1);
        } else if (tok.size() == 2 && hex_value(tok[0]) >= 0 && hex_value(tok[1]) >= 0) {
            p.bytes.push_back(static_cast<uint8_t>(hex_value(tok[0]) * 16 + hex_value(tok[1])));
            p.wild.push_back(0);
        } else {
            return std::nullopt;
        }
        i = j;
    }
    if (p.bytes.empty()) return std::nullopt;
    return p;
}

std::vector<size_t> scan(const uint8_t* data, size_t size, const Pattern& p, size_t max_matches) {
    std::vector<size_t> out;
    const size_t n = p.bytes.size();
    if (n == 0 || size < n || max_matches == 0) return out;
    for (size_t i = 0; i + n <= size; ++i) {
        size_t k = 0;
        while (k < n && (p.wild[k] || data[i + k] == p.bytes[k])) ++k;
        if (k == n) {
            out.push_back(i);
            if (out.size() >= max_matches) break;
        }
    }
    return out;
}

std::optional<int64_t> resolve_rel32(const uint8_t* data, size_t size, size_t match, size_t disp_off,
                                     size_t instr_len) {
    if (match + disp_off + 4 > size) return std::nullopt;
    int32_t rel;
    std::memcpy(&rel, data + match + disp_off, sizeof rel);
    return static_cast<int64_t>(match) + static_cast<int64_t>(instr_len) + rel;
}

} // namespace vh
