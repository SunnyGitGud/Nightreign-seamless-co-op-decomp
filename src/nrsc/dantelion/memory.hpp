#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

// nrsc\dantelion\memory\memory.cpp: the engine memory-block resize at the end of the hook installer
// FUN_18001edb0 (0x180021204..0x180021626; evidence/pseudocode/0x18001edb0.c,
// ev_468cd6057f6cc5ce9379d19d17110613e48875b7ecbfb691c5ed7ecbf63a03ca). See docs/dantelion_memory.md.
namespace nrsc::dantelion {

inline constexpr std::size_t kMemoryBlockCount = 29;                 // index > 0x1c is fatal
inline constexpr std::u16string_view kNetworkBlock = u"NETWORK";     // .rdata 0x18014e23a
inline constexpr std::uint64_t kNetworkBlockSize = 0x1e00000;        // 30 MiB
inline constexpr std::size_t kProtectLength = 0x1d;                  // bytes, as passed (not 29 * 8)
inline constexpr std::uint32_t kPageExecuteReadWrite = 0x40;

// The engine's component table, reached through *(installer_ctx + 0xb8): two parallel arrays.
struct MemoryBlockTable {
    const char16_t* const* names;  // +0x00: UTF-16 names
    std::uint64_t* sizes;          // +0x08
};

// VirtualProtect(address, length, protection) -> previous protection.
using ProtectMemory = std::function<std::uint32_t(void* address, std::size_t length, std::uint32_t protection)>;

// Formatted with "%ws" into 0x100 bytes.
std::string block_not_found_message(std::u16string_view name);

// Linear search over the first 29 names for an exact match. Not found is fatal:
// FatalError(block_not_found_message(name), memory.cpp, 0x13).
std::size_t find_memory_block(const MemoryBlockTable& table, std::u16string_view name);

// Find `name`, then: protect(sizes, 0x1d, 0x40); sizes[i] = size; protect(sizes, 0x1d, old).
void set_memory_block_size(const MemoryBlockTable& table, std::u16string_view name, std::uint64_t size,
                           const ProtectMemory& protect);

}  // namespace nrsc::dantelion
