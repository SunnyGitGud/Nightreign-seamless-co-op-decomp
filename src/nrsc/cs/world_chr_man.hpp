#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string_view>

// nrsc\cs\world_chr_man\world_chr_man.cpp: replacement allocators for two WorldChrMan character sets
// (FUN_180026830 PlayerIns, FUN_180026b30 NecroBuddy; ev_98e4828f51b29f1724b1ea4e9d4621b970a1add9786b6b1f10a2c6b699f0bfde).
// Both are hook detours registered from protected code (pointers at .themida 0x1808b6c04 / 0x1808b6c28),
// so the game functions they replace are unknown. See docs/world_chr_man.md.
namespace nrsc::cs {

// The part of the CSChrSet header that the detours write. Field names are descriptive (W).
struct ChrSetHeader {
    std::int32_t base_index = 0;     // +0x08: *(*(WorldChrMan + 0x9910) + 0x120) + index_offset
    std::int32_t reserved = 0;       // +0x0c: always set to -1
    std::uint32_t capacity = 0;      // +0x10
    std::byte* entries = nullptr;    // +0x18: capacity * 0x10 bytes, zeroed
};

inline constexpr std::size_t kChrSetEntrySize = 0x10;  // {u64, u16 @+8, u8 @+0xa}, all zeroed

struct ChrSetSpec {
    std::string_view name;          // in the allocation-failure message
    std::int32_t index_offset;      // added to the WorldChrMan base index
    std::uint32_t capacity;
    unsigned null_manager_line;     // FatalError("WorldChrManImp", world_chr_man.cpp, line)
    unsigned alloc_failure_line;    // FatalError("Cannot create container for CSChrSet.<name>!!", …, line)
};

inline constexpr ChrSetSpec kPlayerInsSet{"PlayerIns", 0, 6, 0x41, 0x4f};
inline constexpr ChrSetSpec kNecroBuddySet{"NecroBuddy", 2, 0x78, 0x5e, 0x6c};

// The game allocator's vtable +0x50: allocate(size, alignment). The detours always pass alignment 8.
using GameAllocate = std::function<std::byte*(std::size_t size, std::size_t alignment)>;

// world_chr_man: the game's WorldChrMan instance (null is fatal). Only *(*(world_chr_man + 0x9910) + 0x120) is read.
void init_chr_set(const ChrSetSpec& spec, const std::byte* world_chr_man, ChrSetHeader& header,
                  const GameAllocate& allocate);

}  // namespace nrsc::cs
