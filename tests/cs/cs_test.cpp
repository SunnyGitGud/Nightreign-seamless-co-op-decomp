// nrsc\cs\world_chr_man, bloodstain_man and auto_control_api detours
// (FUN_180026830, FUN_180026b30, FUN_180027090, FUN_180028c20).
#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/cs/auto_control_api.hpp"
#include "nrsc/cs/bloodstain_man.hpp"
#include "nrsc/cs/world_chr_man.hpp"

using namespace nrsc::cs;
using nrsc::FatalError;

namespace {

// WorldChrMan with *(+0x9910) -> block whose +0x120 is `base`.
struct FakeWorldChrMan {
    std::vector<std::byte> manager = std::vector<std::byte>(0x9918);
    std::vector<std::byte> sets = std::vector<std::byte>(0x128);

    explicit FakeWorldChrMan(std::int32_t base) {
        const std::byte* p = sets.data();
        std::memcpy(manager.data() + 0x9910, &p, 8);
        std::memcpy(sets.data() + 0x120, &base, 4);
    }
};

struct RecordingAllocator {
    std::vector<std::vector<std::byte>> blocks;
    std::size_t last_size = 0, last_alignment = 0;

    std::byte* operator()(std::size_t size, std::size_t alignment) {
        last_size = size;
        last_alignment = alignment;
        blocks.emplace_back(size, std::byte{0xcd});  // dirty, so zeroing is observable
        return blocks.back().data();
    }
};

bool all_zero(const std::byte* p, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i)
        if (p[i] != std::byte{0}) return false;
    return true;
}

std::uint64_t u64(const std::byte* p, std::size_t off) {
    std::uint64_t v;
    std::memcpy(&v, p + off, 8);
    return v;
}

}  // namespace

// --- world_chr_man -------------------------------------------------------------------------------

TEST(WorldChrMan, PlayerSetHasSixSlots) {
    FakeWorldChrMan wcm(40);
    RecordingAllocator alloc;
    ChrSetHeader h;
    init_chr_set(kPlayerInsSet, wcm.manager.data(), h, std::ref(alloc));
    EXPECT_EQ(h.base_index, 40);
    EXPECT_EQ(h.reserved, -1);
    EXPECT_EQ(h.capacity, 6u);
    EXPECT_EQ(alloc.last_size, 0x60u);
    EXPECT_EQ(alloc.last_alignment, 8u);
    ASSERT_NE(h.entries, nullptr);
    EXPECT_TRUE(all_zero(h.entries, 0x60));
}

TEST(WorldChrMan, NecroBuddySetHas120SlotsAndOffsetTwo) {
    FakeWorldChrMan wcm(40);
    RecordingAllocator alloc;
    ChrSetHeader h;
    init_chr_set(kNecroBuddySet, wcm.manager.data(), h, std::ref(alloc));
    EXPECT_EQ(h.base_index, 42);
    EXPECT_EQ(h.capacity, 120u);
    EXPECT_EQ(alloc.last_size, 0x780u);
    EXPECT_TRUE(all_zero(h.entries, 0x780));
}

TEST(WorldChrMan, NullManagerIsFatal) {
    ChrSetHeader h;
    RecordingAllocator alloc;
    for (const auto& [spec, line] : {std::pair{kPlayerInsSet, 0x41u}, std::pair{kNecroBuddySet, 0x5eu}}) {
        try {
            init_chr_set(spec, nullptr, h, std::ref(alloc));
            FAIL();
        } catch (const FatalError& e) {
            EXPECT_STREQ(e.what(), "WorldChrManImp");
            EXPECT_EQ(e.source_file(), "nrsc\\cs\\world_chr_man\\world_chr_man.cpp");
            EXPECT_EQ(e.line(), line);
        }
    }
    EXPECT_TRUE(alloc.blocks.empty());
}

TEST(WorldChrMan, AllocationFailureIsFatal) {
    FakeWorldChrMan wcm(0);
    ChrSetHeader h;
    const GameAllocate fail = [](std::size_t, std::size_t) -> std::byte* { return nullptr; };
    try {
        init_chr_set(kPlayerInsSet, wcm.manager.data(), h, fail);
        FAIL();
    } catch (const FatalError& e) {
        EXPECT_STREQ(e.what(), "Cannot create container for CSChrSet.PlayerIns!!");
        EXPECT_EQ(e.line(), 0x4fu);
    }
    try {
        init_chr_set(kNecroBuddySet, wcm.manager.data(), h, fail);
        FAIL();
    } catch (const FatalError& e) {
        EXPECT_STREQ(e.what(), "Cannot create container for CSChrSet.NecroBuddy!!");
        EXPECT_EQ(e.line(), 0x6cu);
    }
}

// --- bloodstain_man ------------------------------------------------------------------------------

TEST(BloodstainMan, BlockLayout) {
    RecordingAllocator alloc;
    int prepared = 0;
    BloodstainAllocator a{[&](void*) { ++prepared; }, std::ref(alloc)};
    std::byte* b = create_bloodstain_block([&](void*) { return &a; }, nullptr);

    EXPECT_EQ(prepared, 1);
    EXPECT_EQ(alloc.last_size, 0x78u);
    EXPECT_EQ(alloc.last_alignment, 0x10u);
    EXPECT_TRUE(all_zero(b, 0x18));
    EXPECT_EQ(u64(b, 0x18), ~0ull);
    EXPECT_TRUE(all_zero(b + 0x20, 0x10));
    for (std::size_t off = 0x30; off < 0x60; off += 8) EXPECT_EQ(u64(b, off), 0xffffffffull) << off;
    EXPECT_EQ(u64(b, 0x60), 0u);
    EXPECT_EQ(u64(b, 0x68), 6u);
    EXPECT_EQ(u64(b, 0x70), 0u);
}

TEST(BloodstainMan, NoAllocatorIsFatalWithEmptyMessage) {
    try {
        create_bloodstain_block([](void*) -> const BloodstainAllocator* { return nullptr; }, nullptr);
        FAIL();
    } catch (const FatalError& e) {
        EXPECT_STREQ(e.what(), "");
        EXPECT_EQ(e.source_file(), "nrsc\\cs\\bloodstain_man\\bloodstain_man.cpp");
        EXPECT_EQ(e.line(), 0xeu);
    }
}

// --- auto_control_api ----------------------------------------------------------------------------

TEST(AutoControlApi, CallsSignInWithZero) {
    int arg = -1;
    AutoControlMethods methods{{"SignIn", [&](int a) { arg = a; }}, {"Other", [](int) {}}};
    invoke_method(methods, kSignInMethod);
    EXPECT_EQ(arg, 0);
}

TEST(AutoControlApi, MissingMethodIsFatal) {
    try {
        invoke_method({}, kSignInMethod);
        FAIL();
    } catch (const FatalError& e) {
        EXPECT_STREQ(e.what(),
                     "Method \"SignIn\" is not available. If you're using ModEngine, please make sure seamless co-op "
                     "is loaded early. The game will now exit.");
        EXPECT_EQ(e.source_file(), "nrsc\\cs\\auto_control_api\\auto_control_api.cpp");
        EXPECT_EQ(e.line(), 0x14u);
    }
}

TEST(AutoControlApi, MethodNamesAreCaseSensitive) {
    AutoControlMethods methods{{"signin", [](int) {}}};
    EXPECT_THROW(invoke_method(methods, kSignInMethod), FatalError);
}

TEST(AutoControlApi, MessageFitsTheBuffer) {
    EXPECT_LT(method_unavailable_message(kSignInMethod).size(), 0x100u);
    EXPECT_EQ(method_unavailable_message(std::string(300, 'x')).size(), 0xffu);  // W: CRT truncation assumed
}

TEST(AutoControlApi, SignInGate) {
    EXPECT_TRUE(should_sign_in(true, true, true, 0));
    EXPECT_FALSE(should_sign_in(false, true, true, 0));
    EXPECT_FALSE(should_sign_in(true, false, true, 0));
    EXPECT_FALSE(should_sign_in(true, true, false, 0));
    EXPECT_FALSE(should_sign_in(true, true, true, 1));
}
