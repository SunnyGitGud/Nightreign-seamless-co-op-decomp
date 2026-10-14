#include "nrsc/signatures/pattern.hpp"

#include <algorithm>
#include <array>
#include <cstring>

namespace nrsc::signatures {

namespace {

// Hex digit value or -1. Original: '0'-'9', 'a'-'f', 'A'-'F' (asm 0x18002bfd0..0x18002c002).
int hex_value(unsigned char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

constexpr std::size_t kMaxRepeat = 0x100000;

}  // namespace

// FUN_18002bf30 (0x18002bf30), grade C.
std::optional<Pattern> parse_pattern(std::string_view text) {
    Pattern out;
    const std::size_t n = text.size();
    std::size_t pos = 0;
    while (pos < n) {
        const unsigned char c = static_cast<unsigned char>(text[pos]);
        if (c == ' ') {  // asm 0x18002bfc2
            ++pos;
            continue;
        }
        std::uint8_t value = 0;
        std::uint8_t mask = 0;
        const int d = hex_value(c);
        if (d >= 0) {
            value = static_cast<std::uint8_t>(d);
            mask = 0xFF;
        } else if (c != '?') {
            return std::nullopt;  // asm 0x18002bff1
        }
        ++pos;
        // Optional second nibble (asm 0x18002c014..0x18002c074).
        if (pos < n) {
            const unsigned char c2 = static_cast<unsigned char>(text[pos]);
            const int d2 = hex_value(c2);
            if (d2 >= 0) {
                value = static_cast<std::uint8_t>((value << 4) | d2);
                mask = static_cast<std::uint8_t>(mask | 0x0F);
                ++pos;
            } else if (c2 == '?') {
                value = static_cast<std::uint8_t>(value << 4);
                mask = static_cast<std::uint8_t>(mask << 4);
                ++pos;
            }
        }
        // Optional "&MM" mask (asm 0x18002c077..0x18002c10f).
        if (pos < n && text[pos] == '&') {
            if (pos + 1 >= n) return std::nullopt;
            const int hi = hex_value(static_cast<unsigned char>(text[pos + 1]));
            if (hi < 0) return std::nullopt;
            int m = hi;
            pos += 2;
            if (pos < n) {
                const int lo = hex_value(static_cast<unsigned char>(text[pos]));
                if (lo >= 0) {
                    m = (hi << 4) | lo;
                    ++pos;
                }
            }
            mask = static_cast<std::uint8_t>(mask & m);
        }
        // Optional "#N" repeat (asm 0x18002c112..0x18002c166).
        std::size_t repeat = 1;
        if (pos < n && text[pos] == '#') {
            if (pos + 1 >= n) return std::nullopt;
            ++pos;
            repeat = 0;
            while (pos < n && text[pos] >= '0' && text[pos] <= '9') {
                repeat = repeat * 10 + static_cast<std::size_t>(text[pos] - '0');
                if (repeat > kMaxRepeat) return std::nullopt;
                ++pos;
            }
            if (repeat == 0) return std::nullopt;  // asm 0x18002c166
        }
        value = static_cast<std::uint8_t>(value & mask);
        for (std::size_t i = 0; i < repeat; ++i) {
            out.value.push_back(value);
            out.mask.push_back(mask);
        }
    }
    // Trailing fully-wildcard bytes are not compared (asm 0x18002c278..0x18002c298).
    std::size_t len = out.value.size();
    while (len > 0 && out.mask[len - 1] == 0) --len;
    out.effective_length = len;
    return out;
}

bool matches_at(const Pattern& p, std::span<const std::uint8_t> data, std::size_t pos) noexcept {
    for (std::size_t i = 0; i < p.effective_length; ++i)
        if ((data[pos + i] & p.mask[i]) != p.value[i]) return false;
    return true;
}

namespace {

// Pick the exact byte that is rarest in a strided sample of the window, as the original picks the rarest
// byte of the pattern from a static frequency table. Returns npos when the pattern has no exact byte.
std::size_t pick_anchor(const Pattern& p, std::span<const std::uint8_t> window) {
    std::array<std::size_t, 256> hist{};
    const std::size_t step = window.size() / 65536 + 1;
    for (std::size_t i = 0; i < window.size(); i += step) ++hist[window[i]];
    std::size_t best = static_cast<std::size_t>(-1);
    for (std::size_t i = 0; i < p.effective_length; ++i) {
        if (p.mask[i] != 0xFF) continue;
        if (best == static_cast<std::size_t>(-1) || hist[p.value[i]] < hist[p.value[best]]) best = i;
    }
    return best;
}

// Calls `emit(pos)` for every match; stops when emit returns false.
template <typename Emit>
void scan(const Pattern& p, std::span<const std::uint8_t> window, Emit emit) {
    if (p.effective_length == 0 || p.size() > window.size()) return;
    const std::size_t last = window.size() - p.size();  // last candidate position
    const std::size_t anchor = pick_anchor(p, window);
    if (anchor == static_cast<std::size_t>(-1)) {
        for (std::size_t pos = 0; pos <= last; ++pos)
            if (matches_at(p, window, pos) && !emit(pos)) return;
        return;
    }
    const std::uint8_t key = p.value[anchor];
    std::size_t from = anchor;  // first position of the anchor byte to look at
    const std::size_t to = last + anchor + 1;  // one past the last one
    while (from < to) {
        const void* hit = std::memchr(window.data() + from, key, to - from);
        if (!hit) return;
        const std::size_t at = static_cast<std::size_t>(static_cast<const std::uint8_t*>(hit) - window.data());
        const std::size_t pos = at - anchor;
        if (matches_at(p, window, pos) && !emit(pos)) return;
        from = at + 1;
    }
}

}  // namespace

// FUN_18002c340 (0x18002c340), grade C.
std::optional<std::size_t> find_first(const Pattern& p, std::span<const std::uint8_t> window) {
    std::optional<std::size_t> result;
    scan(p, window, [&](std::size_t pos) {
        result = pos;
        return false;
    });
    return result;
}

// FUN_18007cb60 (0x18007cb60), grade C.
std::vector<std::size_t> find_all(const Pattern& p, std::span<const std::uint8_t> window) {
    std::vector<std::size_t> result;
    scan(p, window, [&](std::size_t pos) {
        result.push_back(pos);
        return true;
    });
    return result;
}

}  // namespace nrsc::signatures
