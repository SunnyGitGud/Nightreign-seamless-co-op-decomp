#pragma once
// The signature table built by FUN_1800655a0 / FUN_18006b830 / FUN_18006f570 and its data.
#include <array>
#include <cstdint>
#include <span>
#include <string_view>

#include "nrsc/signatures/module_image.hpp"
#include "nrsc/signatures/resolver.hpp"

namespace nrsc::signatures {

enum class EntryKind : std::uint8_t {
    Find1,    // call of FUN_18007c8f0
    Inline,   // inlined copy of FUN_18007c8f0
    FindAll,  // FUN_18007cb60 into several slots
    Rtti,     // FUN_18007d380 type-descriptor-name to vtable
};

struct SignatureEntry {
    EntryKind kind;
    std::uint64_t lookup_call;         // nrsc.dll VA of the call (join key with evidence/hooks/hooks.json)
    std::uint16_t exec_order;          // 0-based position in execution order
    std::uint16_t exec_seq;            // find1/inline only: 1-based; the failure counter is exec_seq - 1
    std::array<std::uint16_t, 4> slots;  // table offsets written (slot_count valid)
    std::uint8_t slot_count;
    std::int32_t offset;
    std::uint8_t mode;                 // 0, 1, 2 (find1/inline only)
    std::uint16_t fatal_line;          // find1/inline: 1000; find-all overflow line, 0 = none; rtti: 0
    std::string_view text;             // pattern, or RTTI type name (".?AV...")
};

#include "nrsc/signatures/signature_table.inc"

inline constexpr std::span<const SignatureEntry> signature_entries() { return kSignatureEntries; }

// Table layout (docs/signatures.md section 2, grade S): header of 3 u64 (base, size, counter at +0x10), then u64
// result slots at +0x18 .. +0x5d0.
inline constexpr unsigned kFirstSlotOffset = 0x18;
inline constexpr unsigned kLastSlotOffset = 0x5d0;
inline constexpr std::size_t kSlotCount = (kLastSlotOffset - kFirstSlotOffset) / 8 + 1;

class SignatureTable {
public:
    // Runs every entry in execution order (the original's straight-line builder), reproducing the counter and the
    // fatal paths (FatalError from resolver.hpp). Values are VAs in the image (the original stores absolute pointers).
    static SignatureTable build(const ModuleImage& image);

    // Result stored at table offset `table_offset` (0x18..0x5d0, multiple of 8); 0 when unset.
    std::uint64_t at(unsigned table_offset) const;
    std::uint32_t lookups_succeeded() const noexcept { return counter_; }

private:
    std::array<std::uint64_t, kSlotCount> slots_{};
    std::uint32_t counter_ = 0;
};

}  // namespace nrsc::signatures
