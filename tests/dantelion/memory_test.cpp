// nrsc\dantelion\memory\memory.cpp (FUN_18001edb0 tail, 0x180021204..).
#include "nrsc/dantelion/memory.hpp"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <tuple>
#include <vector>

#include "nrsc/core/fatal_error.hpp"

using namespace nrsc::dantelion;
using nrsc::FatalError;

namespace {

struct FakeTable {
    std::array<std::u16string, kMemoryBlockCount> storage;
    std::array<const char16_t*, kMemoryBlockCount + 1> names{};
    std::array<std::uint64_t, kMemoryBlockCount + 1> sizes{};

    FakeTable() {
        for (std::size_t i = 0; i < kMemoryBlockCount; ++i) storage[i] = u"BLOCK" + std::u16string(1, u'A' + i % 26);
        bind();
    }
    void bind() {
        for (std::size_t i = 0; i < kMemoryBlockCount; ++i) names[i] = storage[i].c_str();
    }
    MemoryBlockTable view() { return {names.data(), sizes.data()}; }
};

}  // namespace

TEST(DantelionMemory, SetsTheNetworkBlockTo30MiB) {
    FakeTable t;
    t.storage[11] = u"NETWORK";
    t.bind();
    std::vector<std::tuple<void*, std::size_t, std::uint32_t>> calls;
    const ProtectMemory protect = [&](void* a, std::size_t n, std::uint32_t p) {
        calls.emplace_back(a, n, p);
        return calls.size() == 1 ? 0x04u : 0x40u;  // PAGE_READWRITE before
    };
    set_memory_block_size(t.view(), kNetworkBlock, kNetworkBlockSize, protect);

    EXPECT_EQ(t.sizes[11], 0x1e00000u);
    for (std::size_t i = 0; i < kMemoryBlockCount; ++i)
        if (i != 11) {
            EXPECT_EQ(t.sizes[i], 0u);
        }
    ASSERT_EQ(calls.size(), 2u);
    EXPECT_EQ(calls[0], std::make_tuple(static_cast<void*>(t.sizes.data()), std::size_t{0x1d}, 0x40u));
    EXPECT_EQ(calls[1], std::make_tuple(static_cast<void*>(t.sizes.data()), std::size_t{0x1d}, 0x04u));
}

TEST(DantelionMemory, MatchIsExact) {
    FakeTable t;
    t.storage[0] = u"NETWORKX";
    t.storage[1] = u"NETWOR";
    t.storage[2] = u"network";
    t.storage[3] = u"NETWORK";
    t.bind();
    EXPECT_EQ(find_memory_block(t.view(), kNetworkBlock), 3u);
}

TEST(DantelionMemory, FirstMatchWins) {
    FakeTable t;
    t.storage[5] = u"NETWORK";
    t.storage[9] = u"NETWORK";
    t.bind();
    EXPECT_EQ(find_memory_block(t.view(), kNetworkBlock), 5u);
}

TEST(DantelionMemory, OnlyTwentyNineEntriesAreSearched) {
    FakeTable t;
    t.storage[28] = u"NETWORK";
    t.bind();
    EXPECT_EQ(find_memory_block(t.view(), kNetworkBlock), 28u);

    FakeTable u;
    std::u16string beyond = u"NETWORK";
    u.names[29] = beyond.c_str();  // a 30th entry is never looked at
    EXPECT_THROW(find_memory_block(u.view(), kNetworkBlock), FatalError);
}

TEST(DantelionMemory, MissingBlockIsFatalLine0x13) {
    FakeTable t;
    bool protected_called = false;
    try {
        set_memory_block_size(t.view(), kNetworkBlock, kNetworkBlockSize,
                              [&](void*, std::size_t, std::uint32_t) { protected_called = true; return 0u; });
        FAIL();
    } catch (const FatalError& e) {
        EXPECT_STREQ(e.what(), "Reference to engine memory block component \"NETWORK\" not found");
        EXPECT_EQ(e.source_file(), "nrsc\\dantelion\\memory\\memory.cpp");
        EXPECT_EQ(e.line(), 0x13u);
    }
    EXPECT_FALSE(protected_called);
}
