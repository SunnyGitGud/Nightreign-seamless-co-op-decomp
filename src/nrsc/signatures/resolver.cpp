#include "nrsc/signatures/resolver.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/signatures/pattern.hpp"

namespace nrsc::signatures {

namespace {
[[noreturn]] void fatal(const std::string& message, unsigned line) { throw FatalError(message, kSourceFile, line); }
}  // namespace

// FUN_18007c8f0 (0x18007c8f0), grade C.
std::uint64_t SignatureResolver::resolve(std::string_view pattern_text, std::int64_t offset, int mode) {
    std::uint64_t result = 0;
    const auto pattern = parse_pattern(pattern_text);
    const auto hit = pattern ? find_first(*pattern, image_.bytes()) : std::nullopt;  // asm 0x18007c997
    if (hit) {
        const std::uint64_t a = image_.base() + *hit + static_cast<std::uint64_t>(offset);  // asm 0x18007c9aa
        auto rel32 = [&](std::uint64_t at) {
            const auto v = image_.read_i32(at);
            if (!v) throw std::out_of_range("signature rel32 outside the image");
            return static_cast<std::int64_t>(*v);
        };
        switch (mode) {  // asm 0x18007c9ad..0x18007c9d9
            case 0: result = a; break;
            case 1: result = a + 5 + static_cast<std::uint64_t>(rel32(a + 1)); break;
            case 2: result = a + 7 + static_cast<std::uint64_t>(rel32(a + 3)); break;
            default: fatal("Not a valid signature type", kInvalidModeLine);  // asm 0x18007cb3b
        }
    }
    if (result == 0) {  // asm 0x18007c9df, failure block 0x18007ca9a
        char buf[64];
        std::snprintf(buf, sizeof buf, "Failed to find signature: %u", counter_);  // string at 0x18014c25c
        fatal(buf, kNotFoundLine);
    }
    ++counter_;  // asm 0x18007c9e8
    return result;
}

// FUN_18007cb60 (0x18007cb60), grade C.
void SignatureResolver::find_all(std::string_view pattern_text, std::int64_t offset, std::span<std::uint64_t> slots,
                                 unsigned fatal_line) {
    const auto pattern = parse_pattern(pattern_text);
    if (!pattern) return;
    const auto hits = signatures::find_all(*pattern, image_.bytes());
    for (std::size_t i = 0; i < hits.size(); ++i) {
        if (i >= slots.size()) {
            if (fatal_line == 0) return;
            fatal("incompatible game version: inconsistent pattern result", fatal_line);
        }
        slots[i] = image_.base() + hits[i] + static_cast<std::uint64_t>(offset);
    }
}

// FUN_18007d380 + FUN_18007d520 (0x18007d380, 0x18007d520), grade C.
std::uint64_t SignatureResolver::find_vtable(std::string_view name) const {
    const std::uint64_t base = image_.base();
    const std::uint64_t size = image_.size();
    const std::uint64_t end = base + size;
    const std::uint8_t* bytes = image_.bytes().data();
    for (std::uint64_t off = 0; off + 8 <= size; off += 8) {  // asm 0x18007d3c0
        std::uint64_t p;
        std::memcpy(&p, bytes + off, 8);
        if (p > 0xFFFFFFFFFFFFFFE7ull || p < base || p + 0x18 > end) continue;  // asm 0x18007d3d8..0x18007d3ed
        std::uint32_t sig, self_rva, td_rva;
        std::memcpy(&sig, bytes + (p - base), 4);
        if (sig != 1) continue;  // x64 COL signature, asm 0x18007d3ef
        std::memcpy(&self_rva, bytes + (p - base) + 0x14, 4);
        if (base + self_rva != p) continue;  // pSelf, asm 0x18007d3f4..0x18007d410
        std::memcpy(&td_rva, bytes + (p - base) + 0x0c, 4);
        const std::uint64_t td = base + td_rva;  // pTypeDescriptor, asm 0x18007d412
        if (td_rva == 0 || td >= end || td + 0x10 > end) continue;
        const char* s = reinterpret_cast<const char*>(bytes + (td - base) + 0x10);
        const std::size_t room = static_cast<std::size_t>(end - td - 0x10);
        const void* nul = std::memchr(s, 0, room);
        if (!nul) continue;
        const std::string_view type_name(s, static_cast<std::size_t>(static_cast<const char*>(nul) - s));
        if (type_name.substr(0, 2) != ".?") continue;  // strncmp(.., ".?", 2), asm 0x18007d434
        if (type_name == name) return base + off + 8;   // callback FUN_18007d520: strcmp == 0 -> slot + 8
    }
    return 0;
}

}  // namespace nrsc::signatures
