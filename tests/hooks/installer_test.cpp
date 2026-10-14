// hooks::Installer against real machine code in an RWX mmap page (Linux, x86-64 only).
#include <gtest/gtest.h>

#pragma GCC diagnostic ignored "-Wmissing-field-initializers"

#include <sys/mman.h>

#include <cstring>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/hooks/hook.hpp"

using namespace nrsc::hooks;

#if defined(__x86_64__)
namespace {

int detour_returns_7() { return 7; }
int detour_returns_9() { return 9; }

struct Page {
    std::uint8_t* base;
    Page() {
        void* p = mmap(nullptr, 0x1000, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        EXPECT_NE(p, MAP_FAILED);
        base = static_cast<std::uint8_t*>(p);
    }
    ~Page() { munmap(base, 0x1000); }
    std::uintptr_t at(std::size_t off) const { return reinterpret_cast<std::uintptr_t>(base + off); }
    void put(std::size_t off, std::initializer_list<int> b) {
        for (int x : b) base[off++] = std::uint8_t(x);
    }
    void rel32(std::size_t off, std::uint8_t op, std::size_t target) {
        base[off] = op;
        std::int32_t rel = std::int32_t(target) - std::int32_t(off + 5);
        std::memcpy(base + off + 1, &rel, 4);
    }
    template <class F> F fn(std::size_t off) const { return reinterpret_cast<F>(base + off); }
};

// Layout: 0x000 callee (mov eax,1; ret); 0x100 caller (sub rsp,8; call callee; add rsp,8; ret);
//         0x200 jumper (jmp callee); 0x300 entry function (mov eax,1; ret + padding); 0x400 vtable.
void layout(Page& pg) {
    pg.put(0x000, {0xB8, 1, 0, 0, 0, 0xC3});
    pg.put(0x100, {0x48, 0x83, 0xEC, 0x08});
    pg.rel32(0x104, 0xE8, 0x000);
    pg.put(0x109, {0x48, 0x83, 0xC4, 0x08, 0xC3});
    pg.rel32(0x200, 0xE9, 0x000);
    pg.put(0x300, {0xB8, 1, 0, 0, 0, 0xC3, 0x90, 0x90});
    auto f = reinterpret_cast<std::uint64_t>(pg.base);
    std::memcpy(pg.base + 0x400, &f, 8);
}

TEST(Installer, CallSiteReachesDetourAndOriginalWorks) {
    Page pg;
    layout(pg);
    Installer inst(nrsc::platform::process_code_memory());
    void* original = nullptr;
    inst.install({"call", Kind::CallSite, pg.at(0x104), reinterpret_cast<void*>(&detour_returns_7), &original, {}, {0xE8}});
    EXPECT_EQ(pg.fn<int (*)()>(0x100)(), 7);
    ASSERT_EQ(original, pg.base);
    EXPECT_EQ(reinterpret_cast<int (*)()>(original)(), 1);
    EXPECT_EQ(inst.installed(), 1u);
}

TEST(Installer, JmpSiteReachesDetourAndOriginalWorks) {
    Page pg;
    layout(pg);
    Installer inst(nrsc::platform::process_code_memory());
    void* original = nullptr;
    inst.install({"jmp", Kind::JmpSite, pg.at(0x200), reinterpret_cast<void*>(&detour_returns_7), &original});
    EXPECT_EQ(pg.fn<int (*)()>(0x200)(), 7);
    EXPECT_EQ(reinterpret_cast<int (*)()>(original)(), 1);
}

TEST(Installer, WrongOpcodeIsFatal) {
    Page pg;
    layout(pg);
    Installer inst(nrsc::platform::process_code_memory());
    EXPECT_THROW(inst.install({"bad", Kind::CallSite, pg.at(0x200), reinterpret_cast<void*>(&detour_returns_7)}), nrsc::FatalError);
    EXPECT_THROW(inst.install({"bad", Kind::JmpSite, pg.at(0x104), reinterpret_cast<void*>(&detour_returns_7)}), nrsc::FatalError);
}

TEST(Installer, JmpEntryReplacesFunction) {
    Page pg;
    layout(pg);
    Installer inst(nrsc::platform::process_code_memory());
    void* original = nullptr;
    inst.install({"entry", Kind::JmpEntry, pg.at(0x300), reinterpret_cast<void*>(&detour_returns_7), &original});
    EXPECT_EQ(pg.fn<int (*)()>(0x300)(), 7);
    EXPECT_EQ(original, nullptr);
    EXPECT_EQ(pg.base[0x300], 0xE9);
}

TEST(Installer, VtableSlotSwap) {
    Page pg;
    layout(pg);
    Installer inst(nrsc::platform::process_code_memory());
    void* original = nullptr;
    inst.install({"vt", Kind::VtableSlot, pg.at(0x400), reinterpret_cast<void*>(&detour_returns_9), &original});
    std::uint64_t slot;
    std::memcpy(&slot, pg.base + 0x400, 8);
    EXPECT_EQ(slot, reinterpret_cast<std::uint64_t>(&detour_returns_9));
    EXPECT_EQ(original, pg.base);
    EXPECT_EQ(reinterpret_cast<int (*)()>(original)(), 1);
}

TEST(Installer, BytePatchAndExpect) {
    Page pg;
    layout(pg);
    Installer inst(nrsc::platform::process_code_memory());
    inst.install({"patch", Kind::BytePatch, pg.at(0x001), nullptr, nullptr, {2, 0, 0, 0}, {1, 0, 0, 0}});
    EXPECT_EQ(pg.fn<int (*)()>(0x000)(), 2);
    // expect now mismatches
    EXPECT_THROW(inst.install({"patch", Kind::BytePatch, pg.at(0x001), nullptr, nullptr, {3, 0, 0, 0}, {1, 0, 0, 0}}),
                 nrsc::FatalError);
    EXPECT_EQ(pg.fn<int (*)()>(0x000)(), 2);
    try {
        inst.install({"named", Kind::CallSite, pg.at(0x104), reinterpret_cast<void*>(&detour_returns_7), nullptr, {}, {0x00}});
        FAIL();
    } catch (const nrsc::FatalError& e) {
        EXPECT_STREQ(e.what(), "hook failed: named");
        EXPECT_EQ(e.source_file(), "nrsc\\hooks\\hooks.cpp");
    }
}

struct FakeBackend : InlineBackend {
    void* last_target = nullptr;
    bool enabled = false;
    void* create(void* target, void*) override { last_target = target; return reinterpret_cast<void*>(0x1234); }
    void enable_all() override { enabled = true; }
};

TEST(Installer, InlineDelegatesToBackend) {
    Page pg;
    FakeBackend backend;
    Installer inst(nrsc::platform::process_code_memory(), &backend);
    void* original = nullptr;
    inst.install({"inl", Kind::Inline, pg.at(0), reinterpret_cast<void*>(&detour_returns_7), &original});
    EXPECT_EQ(original, reinterpret_cast<void*>(0x1234));
    EXPECT_EQ(backend.last_target, pg.base);
    inst.enable_inline();
    EXPECT_TRUE(backend.enabled);

    Installer none(nrsc::platform::process_code_memory());
    EXPECT_THROW(none.install({"inl", Kind::Inline, pg.at(0), reinterpret_cast<void*>(&detour_returns_7)}), nrsc::FatalError);
}

TEST(Installer, RelayArenaReusedForNearbySites) {
    Page pg;
    layout(pg);
    Installer inst(nrsc::platform::process_code_memory());
    inst.install({"a", Kind::CallSite, pg.at(0x104), reinterpret_cast<void*>(&detour_returns_7)});
    inst.install({"b", Kind::JmpSite, pg.at(0x200), reinterpret_cast<void*>(&detour_returns_9)});
    std::int32_t ra, rb;
    std::memcpy(&ra, pg.base + 0x105, 4);
    std::memcpy(&rb, pg.base + 0x201, 4);
    EXPECT_EQ((0x109 + ra) + 16, (0x205 + rb) - 0);  // consecutive 16-byte relays: b's relay follows a's
    EXPECT_EQ(pg.fn<int (*)()>(0x200)(), 9);
    EXPECT_EQ(pg.fn<int (*)()>(0x100)(), 7);
}

}  // namespace
#endif
