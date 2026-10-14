#include "nrsc/core/raw_memory.hpp"

#include <array>
#include <cstdint>

#include <gtest/gtest.h>

namespace nrsc::raw {
namespace {

TEST(RawMemory, LoadsLittleEndianFieldsAtUnalignedOffsets) {
    const std::array<std::uint8_t, 8> bytes{0xff, 0x78, 0x56, 0x34, 0x12, 0xaa, 0xbb, 0xcc};
    EXPECT_EQ(load<std::uint32_t>(bytes.data(), 1), 0x12345678u);
    EXPECT_EQ(load<std::uint8_t>(bytes.data()), 0xffu);  // offset defaults to 0
}

TEST(RawMemory, StoreWritesExactlySizeofTBytes) {
    std::array<std::uint8_t, 8> bytes{};
    bytes.fill(0xee);
    store<std::uint16_t>(bytes.data(), 3, 0xbeef);
    EXPECT_EQ(bytes, (std::array<std::uint8_t, 8>{0xee, 0xee, 0xee, 0xef, 0xbe, 0xee, 0xee, 0xee}));
}

TEST(RawMemory, FloatRoundTripsBitExactly) {
    std::array<std::uint8_t, 6> bytes{};
    store<float>(bytes.data(), 1, -0.0f);
    EXPECT_EQ(load<std::uint32_t>(bytes.data(), 1), 0x80000000u);
}

TEST(RawMemory, LoadPtrReadsAPointerField) {
    int target = 0;
    std::array<std::uint8_t, 16> bytes{};
    store<const void*>(bytes.data(), 5, &target);
    EXPECT_EQ(load_ptr(bytes.data(), 5), &target);
}

}  // namespace
}  // namespace nrsc::raw
