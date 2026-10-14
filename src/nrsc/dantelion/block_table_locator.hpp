#pragma once
// Locates the engine memory-block table that the original nrsc.dll reaches through *(installer_ctx + 0xb8)
// (docs/dantelion_memory.md, docs/wiring_net.md section 1).
//
// The table is not a heap object in the game: it is two adjacent .data arrays of nightreign.exe v1.03.3,
//   names  = 30 x (const wchar_t*) at 0x1439d9b50, entry 17 -> L"NETWORK"
//   sizes  = 30 x u64            at 0x1439d9c40 (= names + 0xf0), row 0 of a [config][30] size matrix
// The signature table already finds the names array: slot 0x1f0 (`4C 8D 15 ?? ?? ?? ?? 48 8B D9`, mode 2, unique).
#include <cstdint>
#include <optional>

#include "nrsc/dantelion/memory.hpp"
#include "nrsc/signatures/module_image.hpp"
#include "nrsc/signatures/signature_table.hpp"

namespace nrsc::dantelion {

inline constexpr unsigned kSlotBlockNames = 0x1f0;                    // signature-table slot: the names array
inline constexpr std::uint64_t kSizesFromNames = 30 * 8;  // sizes = names + 0xf0
inline constexpr std::size_t kBlockArrayLength = 30;                  // entries in both arrays

struct BlockTableAddresses {
    std::uint64_t names = 0;  // VA of the names array
    std::uint64_t sizes = 0;  // VA of the sizes array (row 0)
};

// Pure part, testable on a synthetic image: validates that `names` holds 30 pointers into the image, that the name at
// index 0 is L"MAIN" and that L"NETWORK" is present. nullopt otherwise.
std::optional<BlockTableAddresses> validate_block_table(const signatures::ModuleImage& image, std::uint64_t names_va);
// Same, with the names VA taken from signature-table slot 0x1f0.
std::optional<BlockTableAddresses> locate_block_table(const signatures::ModuleImage& image,
                                                      const signatures::SignatureTable& table);

// Returns a table over the live arrays (addresses are cast from the image VAs, so `image` must describe the running
// module), or nullptr when not found. The returned object has static storage and is overwritten by the next call.
const MemoryBlockTable* find_block_table(const signatures::ModuleImage& image,
                                         const signatures::SignatureTable& table);

}  // namespace nrsc::dantelion
