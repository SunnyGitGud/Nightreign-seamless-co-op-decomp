#pragma once
// Pattern16 byte-pattern grammar, parser and scanner of nrsc\signatures\signatures.cpp.
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace nrsc::signatures {

// A parsed pattern: value[i] is already ANDed with mask[i]; a byte matches when (mem & mask) == value.
struct Pattern {
    std::vector<std::uint8_t> value;
    std::vector<std::uint8_t> mask;
    // Length actually compared: the pattern without its trailing fully-wildcard bytes (original [pattern+0x30]).
    // The window must still be able to hold the whole pattern (value.size() bytes). 0 means "never matches".
    std::size_t effective_length = 0;

    bool empty() const noexcept { return value.empty(); }
    std::size_t size() const noexcept { return value.size(); }
};

// Parser FUN_18002bf30 (nrsc.dll 0x18002bf30). Grade C (asm read byte by byte).
// Grammar, applied token after token; only the space character 0x20 separates/is skipped:
//   token  := nibble [nibble] [ '&' hex [hex] ] [ '#' decimal ]
//   nibble := hex digit (either case) | '?'
//   "XX" exact byte, "?" or "??" full wildcard, "X?" high nibble only (mask F0), "?X" low nibble only (mask 0F).
//   A single hex digit not followed by a nibble is the exact byte 0x0X. A single '?' is a full wildcard.
//   "&MM": mask = mask & MM (a single hex digit after '&' is the mask value 0x0M).
//   "#N":  repeat the token N times (decimal; N == 0 or no digits is an error).
// Errors: any other character, '&' or '#' at the end of the text, '&' not followed by a hex digit.
// The original reports an error by returning an empty pattern, which then never matches; here the error is
// std::nullopt. Deviation (W): repeat counts above 0x100000 are rejected (the original would try to allocate).
// Empty or all-space text parses to an empty pattern (valid, never matches).
std::optional<Pattern> parse_pattern(std::string_view text);

// True when `p` occurs at data[pos..] (caller guarantees pos + p.size() <= data.size()). Compares only the
// first effective_length bytes, (mem & mask) == value.
bool matches_at(const Pattern& p, std::span<const std::uint8_t> data, std::size_t pos) noexcept;

// FUN_18002c340 (0x18002c340), grade C. First match in the forward window `window` (start = window[0],
// size = window.size()): candidate positions are [0, size - p.size()]; none when the pattern is empty,
// effective_length is 0, or the pattern is longer than the window. Returns the offset in the window.
// The original starts from the rarest byte (table 0x18014ad50); only the result is reproduced.
std::optional<std::size_t> find_first(const Pattern& p, std::span<const std::uint8_t> window);

// FUN_18007cb60 (0x18007cb60), grade C. Every match in the window, ascending, overlapping (the original
// rescans from match + 1), as offsets in the window.
std::vector<std::size_t> find_all(const Pattern& p, std::span<const std::uint8_t> window);

}  // namespace nrsc::signatures
