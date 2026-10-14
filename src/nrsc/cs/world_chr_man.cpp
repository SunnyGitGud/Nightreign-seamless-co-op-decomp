#include "nrsc/cs/world_chr_man.hpp"

#include <cstring>
#include <string>

#include "nrsc/core/fatal_error.hpp"

namespace nrsc::cs {

namespace {

constexpr const char* kSourceFile = "nrsc\\cs\\world_chr_man\\world_chr_man.cpp";

template <typename T>
T load(const std::byte* p) {
    T v;
    std::memcpy(&v, p, sizeof v);
    return v;
}

}  // namespace

void init_chr_set(const ChrSetSpec& spec, const std::byte* world_chr_man, ChrSetHeader& header,
                  const GameAllocate& allocate) {
    if (world_chr_man == nullptr) throw FatalError("WorldChrManImp", kSourceFile, spec.null_manager_line);

    const auto* sets = load<const std::byte*>(world_chr_man + 0x9910);
    header.base_index = load<std::int32_t>(sets + 0x120) + spec.index_offset;
    header.reserved = -1;  // one 64-bit store: (capacity << 32) | 0xffffffff
    header.capacity = spec.capacity;

    std::byte* entries = allocate(spec.capacity * kChrSetEntrySize, 8);
    if (entries == nullptr)
        throw FatalError("Cannot create container for CSChrSet." + std::string(spec.name) + "!!", kSourceFile,
                         spec.alloc_failure_line);
    std::memset(entries, 0, spec.capacity * kChrSetEntrySize);
    header.entries = entries;
}

}  // namespace nrsc::cs
