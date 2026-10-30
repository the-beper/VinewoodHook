#include "core/hashmap.h"

#include <charconv>

namespace vh {

namespace {

bool parse_hex(std::string_view s, uint64_t& out) {
    auto [end, ec] = std::from_chars(s.data(), s.data() + s.size(), out, 16);
    return ec == std::errc() && end == s.data() + s.size();
}

} // namespace

size_t HashMap::load_text(std::string_view text) {
    size_t added = 0;
    while (!text.empty()) {
        const size_t eol = text.find('\n');
        std::string_view line = text.substr(0, eol);
        text = eol == std::string_view::npos ? std::string_view() : text.substr(eol + 1);
        while (!line.empty() && (line.back() == '\r' || line.back() == ' ')) line.remove_suffix(1);

        const size_t sep = line.find(' ');
        if (sep == std::string_view::npos) continue;
        uint64_t pub, cur;
        if (!parse_hex(line.substr(0, sep), pub) || !parse_hex(line.substr(sep + 1), cur)) continue;
        map_[pub] = cur;
        ++added;
    }
    return added;
}

uint64_t HashMap::translate(uint64_t public_hash) const {
    auto it = map_.find(public_hash);
    return it == map_.end() ? public_hash : it->second;
}

} // namespace vh
