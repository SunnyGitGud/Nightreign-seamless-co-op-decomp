// wire(): the non-session NetState members (docs/wiring_net.md).
#include <gtest/gtest.h>

#include <cstdlib>
#include <cstring>
#include <map>

#include "nrsc/hooks/net/net_group.hpp"

using namespace nrsc;
using namespace nrsc::hooks::net;

namespace {

struct FakeAlloc {
    const GameAllocatorVtbl* vtbl;
};
std::size_t g_last_size = 0, g_last_align = 0;
alignas(8) std::uint8_t g_arena[0x400];
void* fake_allocate(GameAllocator*, std::size_t size, std::size_t align) {
    g_last_size = size;
    g_last_align = align;
    return g_arena;
}
const GameAllocatorVtbl g_vtbl_a = [] {
    GameAllocatorVtbl v{};
    v.allocate = &fake_allocate;
    return v;
}();
FakeAlloc g_alloc_a{&g_vtbl_a};

// Data allocator: vtbl[1](alloc, size)
void* fake_data_allocate(GameAllocator*, std::size_t size) {
    g_last_size = size;
    return g_arena + 0x200;
}
void* g_data_vtbl[2] = {nullptr, reinterpret_cast<void*>(&fake_data_allocate)};
struct FakeData {
    void* const* vtbl;
};
FakeData g_alloc_b{g_data_vtbl};

GameAllocator* getter_a() { return reinterpret_cast<GameAllocator*>(&g_alloc_a); }
GameAllocator* getter_b() { return reinterpret_cast<GameAllocator*>(&g_alloc_b); }
void* fake_wrap(void**, void* cb) { return cb; }
void fake_attach(void*, void*) {}
void fake_team_send(void*, void*, std::uint32_t, const void*, std::uint32_t) {}
bool fake_player_send(void*, void*, std::uint32_t, void*, std::uint32_t) { return true; }

std::uint64_t addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

std::map<unsigned, std::uint64_t> full_slots() {
    int dummy_global = 0;
    static void* game_data_global = nullptr;
    (void)dummy_global;
    return {{kSlotGameDataGlobal, addr(&game_data_global)},
            {kSlotAllocatorGetterA, reinterpret_cast<std::uintptr_t>(&getter_a)},
            {kSlotAllocatorGetterB, reinterpret_cast<std::uintptr_t>(&getter_b)},
            {kSlotWrapCallback, reinterpret_cast<std::uintptr_t>(&fake_wrap)},
            {kSlotAttachCallback, reinterpret_cast<std::uintptr_t>(&fake_attach)},
            {kSlotDecreaseJobVtbl, 0x1000},
            {kSlotUpdateJobVtbl, 0x2000},
            {kSlotTeamSendFn, reinterpret_cast<std::uintptr_t>(&fake_team_send)},
            {kSlotPlayerSendFn, reinterpret_cast<std::uintptr_t>(&fake_player_send)}};
}

std::function<std::uint64_t(unsigned)> reader(std::map<unsigned, std::uint64_t> m) {
    return [m](unsigned off) { auto it = m.find(off); return it == m.end() ? 0 : it->second; };
}

}  // namespace

TEST(NetWire, FillsEveryMemberFromItsSlot) {
    NetState s;
    wire_from_slots(reader(full_slots()), s);
    EXPECT_TRUE(s.jobs.decrease_ready());
    EXPECT_TRUE(s.jobs.update_ready());
    EXPECT_EQ(s.jobs.decrease_vtbl, reinterpret_cast<const void*>(0x1000));
    EXPECT_EQ(s.jobs.update_vtbl, reinterpret_cast<const void*>(0x2000));
    EXPECT_EQ(s.team_send, &fake_team_send);
    EXPECT_EQ(s.player_send, &fake_player_send);
    EXPECT_NE(s.game_data_slot, nullptr);
    EXPECT_EQ(s.unicast_id_a.load(), -1);
    EXPECT_EQ(s.unicast_id_b.load(), -1);
    EXPECT_FALSE(s.boss_list.has_value());
    EXPECT_FALSE(s.attached());
}

TEST(NetWire, AllocatorGettersAreCalledThroughTheVtable) {
    NetState s;
    wire_from_slots(reader(full_slots()), s);
    EXPECT_EQ(s.jobs.alloc_object(0x78, 8), g_arena);
    EXPECT_EQ(g_last_size, 0x78u);
    EXPECT_EQ(g_last_align, 8u);
    EXPECT_EQ(s.jobs.alloc_data(0x40), g_arena + 0x200);
    EXPECT_EQ(g_last_size, 0x40u);
}

TEST(NetWire, StateBlobBacksModeWordForceFlagAndJobObject) {
    NetState s;
    wire_from_slots(reader(full_slots()), s);
    EXPECT_EQ(*s.mode_word, 0u);
    EXPECT_EQ(*s.force_flag, 0);
    const std::uint32_t mode = 0x21;
    std::memcpy(s.state_blob.data(), &mode, 4);
    s.state_blob[NetState::kStateForceFlag] = 1;
    EXPECT_EQ(*s.mode_word, 0x21u);
    EXPECT_EQ(*s.force_flag, 1);
    EXPECT_EQ(s.jobs.game_data, s.state_blob.data());
    EXPECT_EQ(s.rate.game_data, s.state_blob.data());
    // the decrease callback flags +0x15a of that object
    LocalJobCallback cb{};
    cb.arg0 = s.jobs.game_data;
    decrease_done_callback(&cb);
    EXPECT_EQ(s.state_blob[layout::kGdmDecreaseDone], 1);
}

TEST(NetWire, MissingSlotsStayVanilla) {
    NetState s;
    wire_from_slots(reader({}), s);
    EXPECT_FALSE(s.jobs.decrease_ready());
    EXPECT_FALSE(s.jobs.update_ready());
    EXPECT_EQ(s.team_send, nullptr);
    EXPECT_EQ(s.player_send, nullptr);
    EXPECT_EQ(s.game_data_slot, nullptr);
}

TEST(NetWire, JobCreationUsesWiredHelpersOnlyWithASession) {
    NetState& s = reset_net_state();
    wire_from_slots(reader(full_slots()), s);
    // no session attached: the detour falls back to the (absent) original and creates nothing
    void* out = nullptr;
    detour_decrease_job(nullptr, &out);
    EXPECT_EQ(out, nullptr);
    reset_net_state();
}

// With NRSC_GAME_EXE: all wired slots resolve to the documented game addresses.
TEST(NetWireRealBinary, SlotsResolveToTheDocumentedAddresses) {
    const char* exe = std::getenv("NRSC_GAME_EXE");
    if (!exe || !*exe) GTEST_SKIP() << "NRSC_GAME_EXE not set";
    const auto image = signatures::load_pe_image(exe);
    const auto table = signatures::SignatureTable::build(image);
    EXPECT_EQ(table.at(kSlotGameDataGlobal), 0x143c078d0u);
    EXPECT_EQ(table.at(kSlotAllocatorGetterA), 0x14080b1d0u);
    EXPECT_EQ(table.at(kSlotAllocatorGetterB), 0x141fc3480u);
    EXPECT_EQ(table.at(kSlotWrapCallback), 0x14053e2b0u);
    EXPECT_EQ(table.at(kSlotAttachCallback), 0x14080e3f0u);
    EXPECT_EQ(table.at(kSlotDecreaseJobVtbl), 0x142c717d8u);
    EXPECT_EQ(table.at(kSlotUpdateJobVtbl), 0x142c71858u);
    EXPECT_EQ(table.at(kSlotTeamSendFn), 0x140dfe250u);
    EXPECT_EQ(table.at(kSlotPlayerSendFn), 0x140df2270u);
    // both allocator getters are `mov rax,[rip+x]; ret`
    for (unsigned off : {kSlotAllocatorGetterA, kSlotAllocatorGetterB}) {
        const std::uint64_t va = table.at(off);
        ASSERT_TRUE(image.contains(va, 8));
        const std::uint8_t* p = image.bytes().data() + (va - image.base());
        EXPECT_EQ(p[0], 0x48);
        EXPECT_EQ(p[1], 0x8b);
        EXPECT_EQ(p[2], 0x05);
        EXPECT_EQ(p[7], 0xc3);
    }
}
