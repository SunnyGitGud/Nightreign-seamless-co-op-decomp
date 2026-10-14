// Tests of the world group's wiring (src/nrsc/hooks/world/wiring.cpp): accessors built from fake signature-table slots
// pointing at fake game globals, and the detours that use them.
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <map>
#include <vector>

#include "nrsc/hooks/world/detours.hpp"
#include "nrsc/hooks/world/game_layout.hpp"
#include "nrsc/hooks/world/wiring.hpp"
#include "nrsc/hooks/world/world_specs.hpp"
#include "nrsc/hooks/world/world_state.hpp"

using namespace nrsc;
using namespace nrsc::hooks::world;
using namespace nrsc::hooks::world::layout;

namespace {

struct WiringTest : ::testing::Test {
    void SetUp() override { reset_state(); }
    void TearDown() override { reset_state(); }
};

std::uint64_t addr(const void* p) { return reinterpret_cast<std::uintptr_t>(p); }

// ---- fakes ----
void* g_global_session = nullptr;
void* g_global_game_data = nullptr;
void* g_global_quick_match = nullptr;
void* g_global_chr_man = nullptr;
void* g_global_world = nullptr;

void* fake_find_chr(void* mgr, std::uint32_t* handle) {
    return mgr && *handle == 0x80000001u ? reinterpret_cast<void*>(0x1234) : nullptr;
}

int g_prepare_arg = 0;
std::uint64_t fake_sign_in_prepare(int arg) { g_prepare_arg = arg; return 7; }

int g_method_arg = -1;
void fake_method(std::uintptr_t arg) { g_method_arg = static_cast<int>(arg); }

bool g_event_flag_on = false;
std::uint32_t g_cond_flag_seen = 0;
std::uint64_t fake_event_flag_evaluate(void* cond, void*, void*) {
    g_cond_flag_seen = load<std::uint32_t>(cond, kCondExpect);
    const bool on = g_event_flag_on && g_cond_flag_seen == kExtendedPartyEventFlag;
    const bool expected = load<std::uint8_t>(cond, kCondValue) != 0;
    auto bits = load<std::uint8_t>(cond, kCondResult);
    bits = static_cast<std::uint8_t>((bits & ~1u) | (on == expected ? 1u : 0u));
    store<std::uint8_t>(cond, kCondResult, bits);
    return 0;
}

// A fake allocator: object whose first qword is a vtable; vtable +0x50 allocate, +0x68 prepare.
std::array<std::byte, 0x100> g_alloc_block{};
void* g_prepared = nullptr;
void* fake_allocate(void*, std::size_t size, std::size_t alignment) {
    EXPECT_EQ(size, 0x78u);
    EXPECT_EQ(alignment, 0x10u);
    return g_alloc_block.data();
}
void fake_prepare(void*, void* block) { g_prepared = block; }
void* fake_allocator_for(void* block) { return block ? g_alloc_block.data() + 0x80 : nullptr; }

std::map<unsigned, std::uint64_t> g_table;
std::uint64_t at(unsigned offset) {
    const auto it = g_table.find(offset);
    return it == g_table.end() ? 0 : it->second;
}

}  // namespace

TEST_F(WiringTest, EmptyTableLeavesVanillaDefaults) {
    g_table.clear();
    wire(&at);
    auto& s = state();
    EXPECT_FALSE(s.session_manager);
    EXPECT_FALSE(s.game_data_man);
    EXPECT_FALSE(s.member_vector_owner);
    EXPECT_FALSE(s.chr_manager);
    EXPECT_FALSE(s.find_chr_by_handle);
    EXPECT_FALSE(s.bloodstain_allocator_for);
    EXPECT_FALSE(s.sign_in_gate);
    EXPECT_FALSE(s.sign_in_prepare);
    EXPECT_FALSE(s.extended_party_preference);
    EXPECT_EQ(s.mod_flags.load(), 0u);
    EXPECT_EQ(s.mode_word.load(), 0u);
    EXPECT_FALSE(s.use_fixed_player_ids);
    EXPECT_TRUE(s.event_flag_7f8_state);  // live byte ctx[0xe8]+0x10 == 1 in every capture
    EXPECT_TRUE(s.fold_player_number_script_ids.empty());
}

TEST_F(WiringTest, GlobalsAreReadThroughTheirSlots) {
    g_table = {{wiring_sig::kSessionManagerGlobal, addr(&g_global_session)},
               {wiring_sig::kGameDataManGlobal, addr(&g_global_game_data)},
               {wiring_sig::kQuickMatchGlobal, addr(&g_global_quick_match)},
               {wiring_sig::kChrManGlobal, addr(&g_global_chr_man)},
               {wiring_sig::kFindChrFn, addr(reinterpret_cast<void*>(&fake_find_chr))}};
    wire(&at);
    auto& s = state();
    int a = 0, b = 0, c = 0, d = 0;
    g_global_session = &a;
    g_global_game_data = &b;
    g_global_quick_match = &c;
    g_global_chr_man = &d;
    EXPECT_EQ(s.session_manager(), &a);
    EXPECT_EQ(s.game_data_man(), &b);
    EXPECT_EQ(s.member_vector_owner(), &c);
    EXPECT_EQ(s.chr_manager(), &d);
    g_global_session = nullptr;  // the singleton slot is read each time
    EXPECT_EQ(s.session_manager(), nullptr);
    std::uint32_t handle = 0x80000001u;
    EXPECT_EQ(s.find_chr_by_handle(&d, &handle), reinterpret_cast<void*>(0x1234));
    handle = 0x80000002u;
    EXPECT_EQ(s.find_chr_by_handle(&d, &handle), nullptr);
}

TEST_F(WiringTest, MemberVectorOwnerDrivesTheLobbyCount) {
    g_table = {{wiring_sig::kQuickMatchGlobal, addr(&g_global_quick_match)}};
    wire(&at);
    std::array<std::byte, 0x1e0> records{};
    std::array<std::byte, 0x80> owner{};
    store<void*>(owner.data(), kMemberVectorBegin, records.data());
    store<void*>(owner.data(), kMemberVectorEnd, records.data() + records.size());
    g_global_quick_match = owner.data();
    EXPECT_EQ(lobby_member_count(), 2u);  // two 0xf0 records, as in the live capture
}

TEST_F(WiringTest, SessionManagerFeedsP7AndP8) {
    g_table = {{wiring_sig::kSessionManagerGlobal, addr(&g_global_session)}};
    wire(&at);
    std::array<std::byte, 0x100> session{};
    store<std::uint32_t>(session.data(), kSessionState, kSessionStateConnected);
    g_global_session = session.data();
    set_party_session_active(true);
    EXPECT_EQ(state().mod_flags.load(), kPartySessionWord);
    EXPECT_EQ(state().mode_word.load(), kPartySessionWord);
    EXPECT_TRUE(detour_p7_is_local_or_owned(nullptr));
    store<std::uint32_t>(session.data(), kSessionState, 1);
    EXPECT_FALSE(detour_p7_is_local_or_owned(nullptr));
    set_party_session_active(false);
    EXPECT_EQ(state().mod_flags.load(), 0u);
    EXPECT_TRUE(detour_p7_is_local_or_owned(nullptr));
}

TEST_F(WiringTest, PartyFlagKeepsHighBitsOfQuickMatchWord) {
    state().quick_match_flags = 0x40;  // lobby flag mirrored by hook #22
    set_party_session_active(true);
    EXPECT_EQ(state().quick_match_flags, 0x40u | kPartySessionWord);
    set_party_session_active(false);
    EXPECT_EQ(state().quick_match_flags, 0x40u);
}

TEST_F(WiringTest, PreferenceIsEventFlagA7F418) {
    std::array<std::byte, 0x20> vtable_stub{};
    g_table = {{sig::kEcEventFlagState, addr(vtable_stub.data())}};
    wire(&at);
    auto& s = state();
    ASSERT_TRUE(s.extended_party_preference);
    EXPECT_FALSE(s.extended_party_preference());  // original not captured yet: off
    s.orig.ec_event_flag_state = &fake_event_flag_evaluate;
    g_event_flag_on = false;
    EXPECT_FALSE(s.extended_party_preference());
    EXPECT_EQ(g_cond_flag_seen, 0xa7f418u);
    g_event_flag_on = true;
    EXPECT_TRUE(s.extended_party_preference());
}

TEST_F(WiringTest, PreferenceDecidesPartyCapOfSortieMemberNum) {
    std::array<std::byte, 0x20> vtable_stub{};
    g_table = {{sig::kEcEventFlagState, addr(vtable_stub.data())}, {wiring_sig::kQuickMatchGlobal, addr(&g_global_quick_match)}};
    wire(&at);
    auto& s = state();
    s.orig.ec_event_flag_state = &fake_event_flag_evaluate;
    std::array<std::byte, 5 * 0xf0> records{};
    std::array<std::byte, 0x80> owner{};
    store<void*>(owner.data(), kMemberVectorBegin, records.data());
    store<void*>(owner.data(), kMemberVectorEnd, records.data() + records.size());
    g_global_quick_match = owner.data();
    std::array<std::byte, kCondSize> cond{};
    store<std::uint32_t>(cond.data(), kCondValue, 3);
    g_event_flag_on = false;
    detour_h27_ec_sortie_member_num(cond.data(), nullptr, nullptr);
    EXPECT_TRUE(load<std::uint8_t>(cond.data(), kCondResult) & 1);  // 5 members seen as 3
    store<std::uint32_t>(cond.data(), kCondValue, 5);
    store<std::uint8_t>(cond.data(), kCondResult, 0);
    g_event_flag_on = true;
    detour_h27_ec_sortie_member_num(cond.data(), nullptr, nullptr);
    EXPECT_TRUE(load<std::uint8_t>(cond.data(), kCondResult) & 1);  // flag on: the real count
}

TEST_F(WiringTest, EventFlag7f8IsAnsweredTrue) {
    wire(&at);
    std::array<std::byte, kCondSize> cond{};
    store<std::uint32_t>(cond.data(), kCondExpect, 0x7f8);
    store<std::uint8_t>(cond.data(), kCondValue, 1);
    detour_h26_ec_event_flag_state(cond.data(), nullptr, nullptr);
    EXPECT_TRUE(load<std::uint8_t>(cond.data(), kCondResult) & 1);
}

TEST_F(WiringTest, BloodstainAllocatorUsesVtableSlots) {
    // allocator object: vtable pointer first; vtable +0x50 allocate, +0x68 prepare
    static std::array<void*, 16> vtable{};
    vtable[0x50 / 8] = reinterpret_cast<void*>(&fake_allocate);
    vtable[0x68 / 8] = reinterpret_cast<void*>(&fake_prepare);
    store<void*>(g_alloc_block.data(), 0x80, vtable.data());
    g_table = {{wiring_sig::kAllocatorForFn, addr(reinterpret_cast<void*>(&fake_allocator_for))}};
    wire(&at);
    int game_block = 0;
    std::byte* result = static_cast<std::byte*>(detour_p12_bloodstain_man_ctor(&game_block));
    EXPECT_EQ(g_prepared, &game_block);
    EXPECT_EQ(result, g_alloc_block.data());
    EXPECT_EQ(load<std::uint64_t>(result, 0x68), 6u);  // 6 slots
}

TEST_F(WiringTest, SignInGateAndCall) {
    std::array<std::byte, 0x17500> world{};
    std::array<std::byte, 0x100> session{};
    g_table = {{wiring_sig::kSignInWorldGlobal, addr(&g_global_world)},
               {wiring_sig::kSessionManagerGlobal, addr(&g_global_session)},
               {wiring_sig::kSignInPrepareFn, addr(reinterpret_cast<void*>(&fake_sign_in_prepare))}};
    wire(&at);
    auto& s = state();
    ASSERT_TRUE(s.sign_in_gate);
    g_global_world = nullptr;
    g_global_session = nullptr;
    SignInGate g = s.sign_in_gate();
    EXPECT_FALSE(g.game_present);
    EXPECT_FALSE(g.sign_in_present);
    g_global_world = world.data();
    g_global_session = session.data();
    g = s.sign_in_gate();
    EXPECT_TRUE(g.game_present);
    EXPECT_FALSE(g.game_field_174e8_set);
    store<std::uint64_t>(world.data(), 0x174e8, 1);
    store<std::int32_t>(session.data(), 0x58, 0);
    g = s.sign_in_gate();
    EXPECT_TRUE(g.game_field_174e8_set);
    EXPECT_TRUE(g.sign_in_present);
    EXPECT_EQ(g.sign_in_state, 0);

    s.registered_methods["SignIn"] = reinterpret_cast<void*>(&fake_method);
    g_prepare_arg = 0;
    g_method_arg = -1;
    EXPECT_FALSE(detour_h6_sign_in(nullptr, nullptr));  // no original: false
    EXPECT_EQ(g_prepare_arg, 1);
    EXPECT_EQ(g_method_arg, 0);

    // state != 0: the gate is closed, nothing is called
    store<std::int32_t>(session.data(), 0x58, 1);
    g_prepare_arg = 0;
    g_method_arg = -1;
    detour_h6_sign_in(nullptr, nullptr);
    EXPECT_EQ(g_prepare_arg, 0);
    EXPECT_EQ(g_method_arg, -1);
}

TEST_F(WiringTest, TaskAdapterFollowsTheTaskLayout) {
    static int register_calls = 0;
    static std::uint32_t register_arg = 0;
    struct Hook {
        static void do_register(void*, std::uint32_t arg) { ++register_calls; register_arg = arg; }
    };
    std::array<std::byte, 0x40> task{};
    store<std::uint32_t>(task.data(), 0x20, 9);
    store<void*>(task.data(), 0x28, reinterpret_cast<void*>(&Hook::do_register));
    const AutoControlTask t = auto_control_task_from_object(task.data());
    EXPECT_FALSE(t.is_registered());
    t.do_register();
    EXPECT_EQ(register_calls, 1);
    EXPECT_EQ(register_arg, 9u);
    store<void*>(task.data(), 0x10, task.data());
    EXPECT_TRUE(t.is_registered());
    EXPECT_FALSE(auto_control_task_from_object(nullptr).is_registered);
}
