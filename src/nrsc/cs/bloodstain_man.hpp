#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>

// nrsc\cs\bloodstain_man\bloodstain_man.cpp: FUN_180027090
// (ev_6539e76aa5b08c41bffaab697186b75175147ef798e46c093b4c0de2dbcd63e1). This is a hook detour that
// allocates the 0x78-byte block for a bloodstain container sized for 6 slots. The hook target is
// unknown. See docs/world_chr_man.md §3.
namespace nrsc::cs {

inline constexpr std::size_t kBloodstainBlockSize = 0x78;
inline constexpr std::size_t kBloodstainBlockAlignment = 0x10;
inline constexpr std::uint64_t kBloodstainSlots = 6;  // stored at +0x68

// Fill a block as the detour does:
//   +0x00..+0x17 0, +0x18 -1 (u64), +0x20..+0x2f 0,
//   +0x30..+0x5f six u64 0x00000000ffffffff, +0x60 0, +0x68 6, +0x70 0
void init_bloodstain_block(std::byte* block);

// The game-side calls, injected:
//   allocator_for(arg)   hooks+0xa8 -> [1]: returns the allocator, or null
//   prepare(arg)         allocator vtable +0x68, called before allocating
//   allocate(size, align) allocator vtable +0x50
struct BloodstainAllocator {
    std::function<void(void* arg)> prepare;
    std::function<std::byte*(std::size_t, std::size_t)> allocate;
};
using AllocatorFor = std::function<const BloodstainAllocator*(void* arg)>;

// A null allocator is fatal: FatalError("", bloodstain_man.cpp, 0xe). The message string at
// .rdata 0x18014dea8 is empty. The allocation result is not checked, as in the original.
std::byte* create_bloodstain_block(const AllocatorFor& allocator_for, void* arg);

}  // namespace nrsc::cs
