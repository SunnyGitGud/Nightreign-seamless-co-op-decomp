#include "nrsc/cs/bloodstain_man.hpp"

#include <cstring>

#include "nrsc/core/fatal_error.hpp"

namespace nrsc::cs {

namespace {

void put64(std::byte* p, std::uint64_t v) { std::memcpy(p, &v, sizeof v); }

}  // namespace

void init_bloodstain_block(std::byte* block) {
    std::memset(block, 0, kBloodstainBlockSize);
    put64(block + 0x18, ~0ull);
    for (std::size_t off = 0x30; off < 0x60; off += 8) put64(block + off, 0xffffffffull);
    put64(block + 0x68, kBloodstainSlots);
}

std::byte* create_bloodstain_block(const AllocatorFor& allocator_for, void* arg) {
    const BloodstainAllocator* allocator = allocator_for(arg);
    if (allocator == nullptr) throw FatalError("", "nrsc\\cs\\bloodstain_man\\bloodstain_man.cpp", 0xe);
    allocator->prepare(arg);
    std::byte* block = allocator->allocate(kBloodstainBlockSize, kBloodstainBlockAlignment);
    init_bloodstain_block(block);
    return block;
}

}  // namespace nrsc::cs
