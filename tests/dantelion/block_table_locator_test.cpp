#include "nrsc/dantelion/block_table_locator.hpp"

#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>

using namespace nrsc;
using namespace nrsc::dantelion;

namespace {

constexpr std::uint64_t kBase = 0x140000000;

// Synthetic image: names array at 0x1000, sizes right after it, strings at 0x2000.
signatures::ModuleImage make_image(const char16_t* network_name = u"NETWORK") {
    std::vector<std::uint8_t> bytes(0x4000, 0);
    auto put64 = [&](std::size_t off, std::uint64_t v) { std::memcpy(&bytes[off], &v, 8); };
    for (std::size_t i = 0; i < kBlockArrayLength; ++i) {
        const std::size_t str = 0x2000 + i * 0x40;
        std::u16string name = i == 0 ? u"MAIN" : i == 17 ? std::u16string(network_name) : u"BLK" + std::u16string(1, u'A' + i);
        std::memcpy(&bytes[str], name.c_str(), (name.size() + 1) * 2);
        put64(0x1000 + i * 8, kBase + str);
        put64(0x1000 + kSizesFromNames + i * 8, 0xf00000);
    }
    return signatures::ModuleImage(kBase, std::move(bytes));
}

}  // namespace

TEST(BlockTableLocator, FindsSizesRightAfterTheNames) {
    const auto image = make_image();
    const auto a = validate_block_table(image, kBase + 0x1000);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->names, kBase + 0x1000);
    EXPECT_EQ(a->sizes, kBase + 0x1000 + 0xf0);
}

TEST(BlockTableLocator, RejectsMissingNetworkBlock) {
    EXPECT_FALSE(validate_block_table(make_image(u"NETWORX"), kBase + 0x1000).has_value());
}

TEST(BlockTableLocator, RejectsBadAddresses) {
    const auto image = make_image();
    EXPECT_FALSE(validate_block_table(image, 0).has_value());
    EXPECT_FALSE(validate_block_table(image, kBase + 0x1001).has_value());  // misaligned
    EXPECT_FALSE(validate_block_table(image, kBase + 0x3f00).has_value());  // runs out of image
    EXPECT_FALSE(validate_block_table(image, kBase + 0x2000).has_value());  // not pointers
}

// With NRSC_GAME_EXE: the real arrays (docs/wiring_net.md section 1).
TEST(BlockTableLocatorRealBinary, SignatureSlotResolvesTheGameArrays) {
    const char* exe = std::getenv("NRSC_GAME_EXE");
    if (!exe || !*exe) GTEST_SKIP() << "NRSC_GAME_EXE not set";
    const auto image = signatures::load_pe_image(exe);
    const auto table = signatures::SignatureTable::build(image);
    const auto a = locate_block_table(image, table);
    ASSERT_TRUE(a.has_value());
    EXPECT_EQ(a->names, 0x1439d9b50u);
    EXPECT_EQ(a->sizes, 0x1439d9c40u);
    EXPECT_EQ(image.read_u64(a->sizes + 17 * 8).value(), 0xf00000u);  // on disk: 15 MiB, the original raises it to 30 MiB
    EXPECT_NE(find_block_table(image, table), nullptr);
}
