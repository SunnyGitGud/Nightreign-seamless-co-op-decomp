#include "nrsc/dantelion/memory.hpp"

#include <cstdio>

#include "nrsc/core/fatal_error.hpp"

namespace nrsc::dantelion {

std::string block_not_found_message(std::u16string_view name) {
    // "%ws" narrows through the CRT; component names are ASCII. Other characters become '?' here (W).
    std::string narrow;
    for (const char16_t c : name) narrow.push_back(c < 0x80 ? static_cast<char>(c) : '?');
    char buffer[0x100] = {};
    std::snprintf(buffer, sizeof buffer, "Reference to engine memory block component \"%s\" not found",
                  narrow.c_str());
    return buffer;
}

std::size_t find_memory_block(const MemoryBlockTable& table, std::u16string_view name) {
    for (std::size_t i = 0; i < kMemoryBlockCount; ++i) {
        if (std::u16string_view(table.names[i]) == name) return i;
    }
    throw FatalError(block_not_found_message(name), "nrsc\\dantelion\\memory\\memory.cpp", 0x13);
}

void set_memory_block_size(const MemoryBlockTable& table, std::u16string_view name, std::uint64_t size,
                           const ProtectMemory& protect) {
    const std::size_t i = find_memory_block(table, name);
    const std::uint32_t old = protect(table.sizes, kProtectLength, kPageExecuteReadWrite);
    table.sizes[i] = size;
    protect(table.sizes, kProtectLength, old);
}

}  // namespace nrsc::dantelion
