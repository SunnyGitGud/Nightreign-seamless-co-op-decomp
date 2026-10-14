#pragma once
// Lookup functions of nrsc\signatures\signatures.cpp: FUN_18007c8f0 (find first + resolve mode),
// FUN_18007cb60 (find all), FUN_18007d380 + FUN_18007d520 (RTTI vtable).
#include <cstdint>
#include <span>
#include <string_view>

#include "nrsc/signatures/module_image.hpp"

namespace nrsc::signatures {

inline constexpr const char* kSourceFile = "nrsc\\signatures\\signatures.cpp";  // .rdata 0x18014c6d9, grade C
inline constexpr unsigned kInvalidModeLine = 993;    // 0x3e1, asm 0x18007cb4f
inline constexpr unsigned kNotFoundLine = 1000;      // 0x3e8, asm 0x18007cb36

// The scan context: the table header of the original (table[0] = scan base, table[1] = scan size,
// table[2] = counter of successful lookups at +0x10). The scan range is the whole mapped image
// (docs/signatures.md section 2: W; the writer of base/size is in protected code).
class SignatureResolver {
public:
    explicit SignatureResolver(const ModuleImage& image) : image_(image) {}

    const ModuleImage& image() const noexcept { return image_; }
    std::uint32_t lookups_succeeded() const noexcept { return counter_; }

    // FUN_18007c8f0 (0x18007c8f0), grade C (the 26 inlined copies behave identically, call sites in the JSON).
    // Finds the first match M (lowest address) of `pattern` in the image, A = M + offset, then
    //   mode 0: A;  mode 1: A + 5 + rel32(A + 1);  mode 2: A + 7 + rel32(A + 3).
    // No match (or a parse failure, or a zero result): FatalError("Failed to find signature: %u") with %u =
    // lookups_succeeded() so far, signatures.cpp line 1000. Checked after the scan: an invalid mode with a match
    // is FatalError("Not a valid signature type"), line 993. Success increments the counter. Returns a VA.
    // A rel32 that lies outside the image is a read fault in the original; here it is a std::out_of_range.
    std::uint64_t resolve(std::string_view pattern, std::int64_t offset, int mode);

    // FUN_18007cb60 (0x18007cb60) plus the per-entry store loops (e.g. asm 0x18007140d, 0x180073637), grade C.
    // Every (overlapping) match + offset is stored into `slots` in ascending order; a slot without a match keeps
    // its value (0 when fresh). More matches than slots: FatalError("incompatible game version: inconsistent
    // pattern result", file, fatal_line). When fatal_line is 0 (the 4-slot entry 0x180073d5f has none: the original's
    // default case is unreachable code, S) extra matches are ignored. Does not touch the counter.
    void find_all(std::string_view pattern, std::int64_t offset, std::span<std::uint64_t> slots, unsigned fatal_line);

    // FUN_18007d380 + FUN_18007d520 (0x18007d380, 0x18007d520), grade C. Walks every 8-byte slot of the image; a slot
    // is accepted when its value p is a VA inside the image with p + 0x18 <= end, u32[p] == 1 (x64 COL signature),
    // base + u32[p+0x14] (pSelf) == p, T = base + u32[p+0xc] (pTypeDescriptor) inside the image, the name at T+0x10
    // starts with ".?" and equals `name`. Result slot + 8 (the vtable, which follows the COL pointer); 0 when
    // missing (silent). The first accepted slot in ascending order wins. A name that is not NUL-terminated inside the
    // image is not accepted (the original has no bound; W).
    std::uint64_t find_vtable(std::string_view name) const;

private:
    const ModuleImage& image_;
    std::uint32_t counter_ = 0;
};

}  // namespace nrsc::signatures
