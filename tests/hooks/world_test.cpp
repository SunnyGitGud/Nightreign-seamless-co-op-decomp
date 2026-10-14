// Tests of the world group (src/nrsc/hooks/world): detours on fake game objects (byte buffers laid out per the documented
// offsets, fake originals as plain functions) and the HookSpecs built from a fake signature lookup.
#include <gtest/gtest.h>

#include <array>
#include <cstring>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/hooks/groups.hpp"
#include "nrsc/hooks/world/detours.hpp"
#include "nrsc/hooks/world/game_layout.hpp"
#include "nrsc/hooks/world/world_specs.hpp"
#include "nrsc/hooks/world/world_state.hpp"

using namespace nrsc;
using namespace nrsc::hooks;
using namespace nrsc::hooks::world;
using namespace nrsc::hooks::world::layout;

namespace {

struct WorldTest : ::testing::Test {
    void SetUp() override { reset_state(); }
    void TearDown() override { reset_state(); }
};

// A fake session manager: state at +0xc, mode at +0x10, players vector at +0x78/+0x80 (0xf0-byte entries, id at +0xd4).
struct FakeSession {
    std::array<std::byte, 0x100> bytes{};
    std::vector<std::byte> players;
    FakeSession(std::uint32_t state_value, std::uint32_t mode, const std::vector<std::uint32_t>& ids) {
        store<std::uint32_t>(bytes.data(), kSessionState, state_value);
        store<std::uint32_t>(bytes.data(), kSessionMode, mode);
        players.assign(ids.size() * kSessionPlayerStride + 1, std::byte{0});
        for (std::size_t i = 0; i < ids.size(); ++i)
            store<std::uint32_t>(players.data(), i * kSessionPlayerStride + kSessionPlayerId, ids[i]);
        store<const void*>(bytes.data(), kSessionPlayersBegin, players.data());
        store<const void*>(bytes.data(), kSessionPlayersEnd, players.data() + ids.size() * kSessionPlayerStride);
    }
    void* get() { return bytes.data(); }
};

// ---- fake originals ----
int g_calls = 0;
void* g_last_arg = nullptr;
void* fake_p0_ctor(void* mem) { ++g_calls; g_last_arg = mem; return mem; }
bool fake_p8(void*, void*) { ++g_calls; return true; }
std::vector<std::uint64_t> g_handles;
void fake_p14(void* obj) { g_handles.push_back(load<std::uint64_t>(obj, 8)); }
std::uint32_t g_p5_target_seen = 0;
std::uint32_t fake_p5(void*, void*, std::int32_t, std::int32_t, std::uint8_t, std::uint8_t, std::uint8_t, std::uint64_t) {
    g_p5_target_seen = static_cast<std::uint32_t>(state().current_target.load());
    return 42;
}
std::uint64_t fake_register(void*, void*, const char*, std::uint64_t a4) { ++g_calls; return a4 + 1; }
void fake_lobby_update(void* self) { store<std::uint32_t>(self, 0x88, load<std::uint32_t>(self, 0x10)); }
std::uint64_t fake_ec_pass(void*, void*, void*) { ++g_calls; return 7; }
// EcTestTeamType original: result is true iff cond.value == 10004.
std::vector<std::uint32_t> g_team_calls;
std::uint64_t fake_team(void* cond, void*, void*) {
    g_team_calls.push_back(load<std::uint32_t>(cond, kCondValue));
    const bool ok = load<std::uint32_t>(cond, kCondValue) == 10004;
    store<std::uint8_t>(cond, kCondResult, ok ? 1 : 0);
    return 0;
}
std::uint64_t fake_flag(void*, void*, void*) { ++g_calls; return 9; }

}  // namespace

// ---------------------------------------------------------------- P0
TEST_F(WorldTest, P0RunsOriginalFirstThenClearsFlagsAndFiveBytes) {
    state().orig.p0_game_data_ctor = &fake_p0_ctor;
    state().player_flags.fill(1);
    std::array<std::uint8_t, 8> flags;
    flags.fill(0xaa);
    std::array<std::byte, 0x458> object{};
    store<void*>(object.data(), kGlobalFlagArray, flags.data());
    g_calls = 0;
    EXPECT_EQ(detour_p0_game_data_ctor(object.data()), object.data());
    EXPECT_EQ(g_calls, 1);
    for (auto f : state().player_flags) EXPECT_EQ(f, 0);
    for (int i = 0; i < 5; ++i) EXPECT_EQ(flags[i], 0);
    EXPECT_EQ(flags[5], 0xaa);
}

// ---------------------------------------------------------------- P5
TEST_F(WorldTest, P5GateAndPublishedTarget) {
    state().orig.p5_battle_action = &fake_p5;
    std::array<std::byte, 16> ptr{};
    store<std::int32_t>(ptr.data(), 8, 1234);
    // mode word 0: only a2 == -1 is dropped.
    EXPECT_EQ(detour_p5_battle_action(nullptr, ptr.data(), -1, 0, 0, 0, 0, 0), 0u);
    EXPECT_EQ(detour_p5_battle_action(nullptr, ptr.data(), 3, 0, 0, 0, 0, 0), 42u);
    EXPECT_EQ(g_p5_target_seen, 1234u);
    EXPECT_EQ(state().current_target.load(), -1);  // cleared after the call
    // bit 0 set requires bit 2.
    state().mode_word = kModeWordBit0;
    EXPECT_EQ(detour_p5_battle_action(nullptr, ptr.data(), 3, 0, 0, 0, 0, 0), 0u);
    state().mode_word = kModeWordBit0 | kModeWordBit2;
    EXPECT_EQ(detour_p5_battle_action(nullptr, nullptr, 3, 0, 0, 0, 0, 0), 42u);
    EXPECT_EQ(g_p5_target_seen, 0xffffffffu);  // null a1 publishes -1
}

// ---------------------------------------------------------------- P6
TEST_F(WorldTest, P6PicksSlotByHandleType) {
    int mgr_object = 0, chr_a = 0, chr_b = 0;
    state().chr_manager = [&] { return static_cast<void*>(&mgr_object); };
    state().find_chr_by_handle = [&](void* mgr, std::uint32_t* h) -> void* {
        EXPECT_EQ(mgr, &mgr_object);
        return (*h >> 28) == 8 ? static_cast<void*>(&chr_a) : static_cast<void*>(&chr_b);
    };
    std::array<void*, 2> out{};
    std::uint32_t h = 0x80000005;
    detour_p6_handle_to_chr(out.data(), &h);
    EXPECT_EQ(out[0], &chr_a);
    EXPECT_EQ(out[1], nullptr);
    h = 0x60000005;
    detour_p6_handle_to_chr(out.data(), &h);
    EXPECT_EQ(out[0], nullptr);
    EXPECT_EQ(out[1], &chr_b);
    h = 0x10000005;  // other types and -1: both outputs zero
    out = {&chr_a, &chr_b};
    detour_p6_handle_to_chr(out.data(), &h);
    EXPECT_EQ(out[0], nullptr);
    EXPECT_EQ(out[1], nullptr);
    h = 0xffffffff;
    detour_p6_handle_to_chr(out.data(), &h);
    EXPECT_EQ(out[0], nullptr);
}

TEST_F(WorldTest, P6WithoutManagerReturnsEmptyPair) {
    std::array<void*, 2> out{&out, &out};
    std::uint32_t h = 0x80000001;
    EXPECT_EQ(detour_p6_handle_to_chr(out.data(), &h), out.data());
    EXPECT_EQ(out[0], nullptr);
    EXPECT_EQ(out[1], nullptr);
}

// ---------------------------------------------------------------- P7 / P8
TEST_F(WorldTest, P7TrueWhenFlagClearElseSessionState) {
    EXPECT_TRUE(detour_p7_is_local_or_owned(nullptr));
    FakeSession sess(3, 0, {});
    state().session_manager = [&] { return sess.get(); };
    state().mod_flags = kModeActiveBit;
    EXPECT_TRUE(detour_p7_is_local_or_owned(nullptr));
    store<std::uint32_t>(sess.get(), kSessionState, 2);
    EXPECT_FALSE(detour_p7_is_local_or_owned(nullptr));
    state().session_manager = nullptr;
    EXPECT_FALSE(detour_p7_is_local_or_owned(nullptr));
}

TEST_F(WorldTest, P8CallsOriginalWhenFlagClear) {
    state().orig.p8_session_contains = &fake_p8;
    g_calls = 0;
    EXPECT_TRUE(detour_p8_session_contains(nullptr, nullptr));
    EXPECT_EQ(g_calls, 1);
    FakeSession sess(2, 0, {});
    state().session_manager = [&] { return sess.get(); };
    state().mod_flags = kModeActiveBit;
    EXPECT_FALSE(detour_p8_session_contains(nullptr, nullptr));  // original not called
    EXPECT_EQ(g_calls, 1);
    store<std::uint32_t>(sess.get(), kSessionState, 3);
    EXPECT_TRUE(detour_p8_session_contains(nullptr, nullptr));
}

// ---------------------------------------------------------------- P10 / P11
TEST_F(WorldTest, P10BuildsSixSlotObject) {
    state().party_member_info_vtable = 0x142c70cb0;
    auto* obj = static_cast<std::byte*>(detour_p10_party_member_info_ctor(nullptr));
    ASSERT_NE(obj, nullptr);
    EXPECT_EQ(state().party_member_info, obj);
    EXPECT_EQ(load<std::uintptr_t>(obj, 0), 0x142c70cb0u);
    for (std::size_t i = 0; i < 6; ++i) {
        const std::size_t b = 0x28 + 0x30 * i;
        EXPECT_EQ(load<std::uint64_t>(obj, b), ~0ull) << i;
        EXPECT_EQ(load<std::uint64_t>(obj, b + 8), 1u);
        EXPECT_EQ(load<std::uint64_t>(obj, b + 0x10), 0u);
        EXPECT_EQ(load<std::uint32_t>(obj, b + 0x1c), ~0u);
        EXPECT_EQ(load<std::uint32_t>(obj, b + 0x20), 0u);
        EXPECT_EQ(load<std::uint32_t>(obj, b + 0x24), ~0u);
        EXPECT_EQ(load<std::uint32_t>(obj, b + 0x28), 0u);
    }
    EXPECT_EQ(load<std::uint64_t>(obj, 0x148), ~0ull);
    EXPECT_EQ(load<std::uint64_t>(obj, 0x150), ~0ull);
    EXPECT_EQ(load<std::uint64_t>(obj, 0x158), 0u);
    EXPECT_EQ(load<std::uint64_t>(obj, 0x168), ~0ull);
    EXPECT_EQ(load<std::uint32_t>(obj, 0x178), ~0u);
    ::operator delete(obj);
}

TEST_F(WorldTest, P11ResetsUsedSlotsAndTail) {
    auto* raw = static_cast<std::byte*>(detour_p10_party_member_info_ctor(nullptr));
    // Dirty slot 4 and the tail.
    store<std::uint64_t>(raw, 0x28 + 0x30 * 4, 77);
    store<std::uint64_t>(raw, 0x28 + 0x30 * 4 + 8, 5);
    store<std::uint32_t>(raw, 0x15c, 2);
    store<std::uint32_t>(raw, 0x160, 9);
    FakeSession sess(0, 0, {});
    state().session_manager = [&] { return sess.get(); };
    detour_p11_party_member_info_reset(raw);
    EXPECT_EQ(load<std::uint64_t>(raw, 0x28 + 0x30 * 4), ~0ull);
    EXPECT_EQ(load<std::uint64_t>(raw, 0x28 + 0x30 * 4 + 8), 1u);
    EXPECT_EQ(load<std::uint32_t>(raw, 0x15c), 0u);
    EXPECT_EQ(load<std::uint32_t>(raw, 0x164), 0u);
    EXPECT_EQ(load<std::uint32_t>(raw, 0x168), ~0u);
    EXPECT_EQ(load<std::uint32_t>(raw, 0x178), ~0u);
    // Mode 5, +0x160 set, +0x15c == 2: the tail is kept.
    store<std::uint32_t>(sess.get(), kSessionMode, 5);
    store<std::uint32_t>(raw, 0x15c, 2);
    store<std::uint32_t>(raw, 0x160, 9);
    detour_p11_party_member_info_reset(raw);
    EXPECT_EQ(load<std::uint32_t>(raw, 0x15c), 2u);
    ::operator delete(raw);
}

// ---------------------------------------------------------------- P12
TEST_F(WorldTest, P12BuildsBloodstainBlock) {
    std::vector<std::byte> block(0x78 + 0x10);
    bool prepared = false;
    cs::BloodstainAllocator alloc;
    alloc.prepare = [&](void*) { prepared = true; };
    alloc.allocate = [&](std::size_t size, std::size_t align) {
        EXPECT_EQ(size, 0x78u);
        EXPECT_EQ(align, 0x10u);
        return block.data();
    };
    state().bloodstain_allocator_for = [&](void*) { return &alloc; };
    auto* p = static_cast<std::byte*>(detour_p12_bloodstain_man_ctor(nullptr));
    EXPECT_TRUE(prepared);
    EXPECT_EQ(p, block.data());
    EXPECT_EQ(load<std::uint64_t>(p, 0x68), 6u);
    EXPECT_EQ(load<std::uint64_t>(p, 0x18), ~0ull);
    EXPECT_EQ(load<std::uint64_t>(p, 0x30), 0xffffffffull);
}

TEST_F(WorldTest, P12NullAllocatorIsFatalWithEmptyMessage) {
    try {
        detour_p12_bloodstain_man_ctor(nullptr);
        FAIL();
    } catch (const FatalError& e) {
        EXPECT_STREQ(e.what(), "");
        EXPECT_EQ(e.line(), 0xeu);
    }
}

// ---------------------------------------------------------------- P13
TEST_F(WorldTest, P13SearchesFiveRecordsAndUsesFlagArray) {
    std::vector<std::byte> records(5 * kPlayerRecordStride);
    std::array<std::byte, 0x458> global{};
    store<void*>(global.data(), kGlobalRecordArray, records.data());
    for (std::uint32_t i = 0; i < 5; ++i) store<std::uint32_t>(records.data(), i * kPlayerRecordStride + 8, 100 + i);
    state().game_data_man = [&] { return static_cast<void*>(global.data()); };
    state().player_flags[4] = 1;
    EXPECT_TRUE(detour_p13_player_flag(104));
    EXPECT_FALSE(detour_p13_player_flag(103));
    EXPECT_FALSE(detour_p13_player_flag(999));
    state().game_data_man = nullptr;
    EXPECT_FALSE(detour_p13_player_flag(104));
}

// ---------------------------------------------------------------- P14
TEST_F(WorldTest, P14RepeatsOriginalForEveryPlayerWithShiftedHandle) {
    state().orig.p14_chr_action = &fake_p14;
    FakeSession sess(3, 0, {2, 5});
    state().session_manager = [&] { return sess.get(); };
    std::array<std::byte, 0x20> obj{};
    store<std::uint64_t>(obj.data(), 8, 1000);
    g_handles.clear();
    detour_p14_chr_action(obj.data());
    EXPECT_EQ(g_handles, (std::vector<std::uint64_t>{1000, 1200, 1500}));
    EXPECT_EQ(load<std::uint64_t>(obj.data(), 8), 1000u);  // restored
    state().use_fixed_player_ids = true;
    g_handles.clear();
    detour_p14_chr_action(obj.data());
    EXPECT_EQ(g_handles.size(), 7u);
    EXPECT_EQ(g_handles.back(), 1600u);
}

// ---------------------------------------------------------------- P15 / P16
TEST_F(WorldTest, P15TestsPointerAtPlusEighteen) {
    std::array<std::byte, 0x20> inner{};
    std::array<std::byte, 0x20> handle{};
    EXPECT_FALSE(detour_p15_effect_handle_valid(handle.data()));
    store<void*>(handle.data(), 0x18, inner.data());
    EXPECT_TRUE(detour_p15_effect_handle_valid(handle.data()));
    store<std::uint32_t>(inner.data(), 0x10, 3);
    EXPECT_FALSE(detour_p15_effect_handle_valid(handle.data()));
}

TEST_F(WorldTest, P16HelperCapsAtThreeOrSix) {
    std::array<std::byte, 0xb0> owner_inner{};
    store<std::uint32_t>(owner_inner.data(), 0x98, 5);
    std::array<std::byte, 0xb0> owner{};
    store<void*>(owner.data(), 0xa8, owner_inner.data());
    const void* a1 = owner.data();
    EXPECT_EQ(p16_slot_group_size(&a1), 3u);
    state().extended_party_preference = [] { return true; };
    EXPECT_EQ(p16_slot_group_size(&a1), 5u);
    const void* none = nullptr;
    EXPECT_EQ(p16_slot_group_size(&none), 1u);
}

// ---------------------------------------------------------------- hooks #1, #17
TEST_F(WorldTest, H1IsPassThrough) {
    state().orig.lua_register = &fake_register;
    g_calls = 0;
    EXPECT_EQ(detour_h1_lua_register(nullptr, nullptr, "x", 9), 10u);
    EXPECT_EQ(g_calls, 1);
}

TEST_F(WorldTest, H17CapturesMethodNamesOnceAndCallsOriginal) {
    state().orig.auto_control_register = &fake_register;
    std::array<std::byte, 0x10> entry_a{}, entry_b{};
    int method_a = 0, method_b = 0;
    store<void*>(entry_a.data(), 8, &method_a);
    store<void*>(entry_b.data(), 8, &method_b);
    g_calls = 0;
    EXPECT_EQ(detour_h17_auto_control_register(nullptr, entry_a.data(), "SignIn", 1), 2u);
    detour_h17_auto_control_register(nullptr, entry_b.data(), "SignIn", 1);  // duplicate name: first one stays
    detour_h17_auto_control_register(nullptr, entry_b.data(), "Other", 1);
    EXPECT_EQ(g_calls, 3);
    ASSERT_EQ(state().registered_methods.size(), 2u);
    EXPECT_EQ(state().registered_methods["SignIn"], &method_a);
    EXPECT_EQ(state().registered_methods["Other"], &method_b);
}

// ---------------------------------------------------------------- hook #6
namespace {
bool fake_trigger(void*, void*) { ++g_calls; return true; }
}  // namespace

TEST_F(WorldTest, H6WithoutGateOnlyForwards) {
    state().orig.sign_in_trigger = &fake_trigger;
    g_calls = 0;
    EXPECT_TRUE(detour_h6_sign_in(nullptr, nullptr));
    EXPECT_EQ(g_calls, 1);
}

TEST_F(WorldTest, H6RegistersTasksAndCallsSignIn) {
    state().orig.sign_in_trigger = &fake_trigger;
    int registered = 0, prepared = 0;
    void* called_entry = nullptr;
    int called_arg = -1;
    state().auto_control_tasks[0].is_registered = [] { return false; };
    state().auto_control_tasks[0].do_register = [&] { ++registered; };
    state().auto_control_tasks[1].is_registered = [] { return true; };
    state().auto_control_tasks[1].do_register = [&] { ++registered; };
    state().sign_in_gate = [] { return SignInGate{true, true, true, 0}; };
    state().sign_in_prepare = [&](int v) { prepared += v; };
    int method = 0;
    state().registered_methods["SignIn"] = &method;
    state().call_registered_method = [&](void* e, int a) { called_entry = e; called_arg = a; };
    g_calls = 0;
    EXPECT_TRUE(detour_h6_sign_in(nullptr, nullptr));
    EXPECT_EQ(registered, 1);
    EXPECT_EQ(prepared, 1);
    EXPECT_EQ(called_entry, &method);
    EXPECT_EQ(called_arg, 0);
    EXPECT_EQ(g_calls, 1);
}

TEST_F(WorldTest, H6GateClosedSkipsSignIn) {
    state().sign_in_gate = [] { return SignInGate{true, true, true, 1}; };  // sign-in object state != 0
    bool called = false;
    state().call_registered_method = [&](void*, int) { called = true; };
    detour_h6_sign_in(nullptr, nullptr);
    EXPECT_FALSE(called);
}

TEST_F(WorldTest, H6MissingMethodIsFatalAndReachesHandler) {
    state().sign_in_gate = [] { return SignInGate{true, true, true, 0}; };
    EXPECT_THROW(detour_h6_sign_in(nullptr, nullptr), FatalError);
    std::string message;
    unsigned line = 0;
    state().on_fatal = [&](const std::string& m, const char*, unsigned l) { message = m; line = l; };
    detour_h6_sign_in(nullptr, nullptr);
    EXPECT_NE(message.find("Method \"SignIn\" is not available"), std::string::npos);
    EXPECT_EQ(line, 0x14u);
}

// ---------------------------------------------------------------- hooks #20-#22
TEST_F(WorldTest, H20ForcesModeTwoBeforeOriginal) {
    state().orig.lobby_update_state = &fake_lobby_update;
    std::array<std::byte, 0x100> lobby{};
    store<std::uint32_t>(lobby.data(), 0x10, 1);
    detour_h20_lobby_update_state(lobby.data());
    EXPECT_EQ(load<std::uint32_t>(lobby.data(), 0x10), 2u);
    EXPECT_EQ(load<std::uint32_t>(lobby.data(), 0x88), 2u);  // original saw mode 2
}

namespace {
struct MemberVector {
    std::vector<std::byte> records;
    std::array<std::byte, 0x40> owner{};
    explicit MemberVector(std::size_t count) : records(count * kMemberRecordSize + 1) {
        store<const void*>(owner.data(), kMemberVectorBegin, records.data());
        store<const void*>(owner.data(), kMemberVectorEnd, records.data() + count * kMemberRecordSize);
    }
};
}  // namespace

TEST_F(WorldTest, H21UsesMemberCountWithStride240) {
    std::array<std::byte, 0x100> lobby{};
    detour_h21_lobby_member_count(lobby.data(), 3);  // no vector: the passed value
    EXPECT_EQ(load<std::uint32_t>(lobby.data(), 0x98), 3u);
    MemberVector members(5);
    state().member_vector_owner = [&] { return static_cast<const void*>(members.owner.data()); };
    EXPECT_EQ(lobby_member_count(), 5u);
    detour_h21_lobby_member_count(lobby.data(), 3);
    EXPECT_EQ(load<std::uint32_t>(lobby.data(), 0x98), 5u);
}

TEST_F(WorldTest, H22MirrorsFlagAsBitSix) {
    std::array<std::byte, 0x100> lobby{};
    state().quick_match_flags = 0xff;
    detour_h22_lobby_flag(lobby.data(), 1);
    EXPECT_EQ(state().quick_match_flags, 0xffu);
    detour_h22_lobby_flag(lobby.data(), 0);
    EXPECT_EQ(state().quick_match_flags, 0xbfu);
    EXPECT_EQ(load<std::uint8_t>(lobby.data(), 0xc0), 0u);
    detour_h22_lobby_flag(lobby.data(), 1);
    EXPECT_EQ(state().quick_match_flags, 0xffu);
    EXPECT_EQ(load<std::uint8_t>(lobby.data(), 0xc0), 1u);
}

// ---------------------------------------------------------------- hooks #23-#27
namespace {
struct EcFixture {
    std::array<std::byte, 0x40> cond{};
    std::array<std::byte, 0x40> ctx{};
    std::array<std::byte, 0x458> global{};
    std::array<std::byte, 0x20> holder{};
    explicit EcFixture(std::uint32_t player_number) {
        store<std::uint32_t>(holder.data(), 8, player_number);
        store<void*>(global.data(), kGlobalLocalPlayerHolder, holder.data());
        state().game_data_man = [this] { return static_cast<void*>(global.data()); };
    }
};
}  // namespace

TEST_F(WorldTest, H23FoldsPlayerNumberForListedScriptsUnlessPreference) {
    EcFixture f(5);
    store<std::int32_t>(f.ctx.data(), kEcCtxScriptId, 77);
    store<std::uint32_t>(f.cond.data(), kCondValue, 2);   // expected number 2
    store<std::uint8_t>(f.cond.data(), kCondExpect, 1);   // result must be true
    detour_h23_ec_player_no(f.cond.data(), nullptr, f.ctx.data());
    EXPECT_EQ(load<std::uint8_t>(f.cond.data(), kCondResult) & 1, 0);  // 5 != 2, id not listed
    state().fold_player_number_script_ids = {77};
    detour_h23_ec_player_no(f.cond.data(), nullptr, f.ctx.data());
    EXPECT_EQ(load<std::uint8_t>(f.cond.data(), kCondResult) & 1, 1);  // 5 % 3 == 2
    state().extended_party_preference = [] { return true; };
    detour_h23_ec_player_no(f.cond.data(), nullptr, f.ctx.data());
    EXPECT_EQ(load<std::uint8_t>(f.cond.data(), kCondResult) & 1, 0);  // preference: no folding
}

TEST_F(WorldTest, H23WithoutGlobalUsesNumberZero) {
    std::array<std::byte, 0x40> cond{}, ctx{};
    store<std::uint32_t>(cond.data(), kCondValue, 0);
    store<std::uint8_t>(cond.data(), kCondExpect, 1);
    store<std::uint8_t>(cond.data(), kCondResult, 0xfe);
    detour_h23_ec_player_no(cond.data(), nullptr, ctx.data());
    EXPECT_EQ(load<std::uint8_t>(cond.data(), kCondResult), 0xff);  // other bits preserved
}

TEST_F(WorldTest, H24PassesThrough) {
    state().orig.ec_chr_dead = &fake_ec_pass;
    g_calls = 0;
    EXPECT_EQ(detour_h24_ec_chr_dead(nullptr, nullptr, nullptr), 7u);
    EXPECT_EQ(g_calls, 1);
}

TEST_F(WorldTest, H25RetriesTeamTypesFor0x4a0) {
    state().orig.ec_team_type = &fake_team;
    std::array<std::byte, 0x40> cond{}, ctx{};
    store<std::uint8_t>(cond.data(), kCondExpect, 2);
    store<std::uint32_t>(cond.data(), kCondValue, 10001);
    store<std::uint32_t>(ctx.data(), kEcCtxScriptId, 0x4a0);
    g_team_calls.clear();
    detour_h25_ec_team_type(cond.data(), nullptr, ctx.data());
    EXPECT_EQ(g_team_calls, (std::vector<std::uint32_t>{10001, 10002, 10003, 10004}));  // stops at the first success
    EXPECT_EQ(load<std::uint8_t>(cond.data(), kCondResult) & 1, 1);
    EXPECT_EQ(load<std::uint32_t>(cond.data(), kCondValue), 10001u);  // the real cond keeps its value
    EXPECT_EQ(state().team_type_ids_seen, (std::vector<std::int32_t>{0x4a0}));
    // Other ids: one call, no retry.
    store<std::uint8_t>(cond.data(), kCondResult, 0);
    store<std::uint32_t>(ctx.data(), kEcCtxScriptId, 5);
    g_team_calls.clear();
    detour_h25_ec_team_type(cond.data(), nullptr, ctx.data());
    EXPECT_EQ(g_team_calls.size(), 1u);
    EXPECT_EQ(state().team_type_ids_seen.size(), 2u);
}

TEST_F(WorldTest, H26AnswersFlag7f8FromStateElseOriginal) {
    state().orig.ec_event_flag_state = &fake_flag;
    std::array<std::byte, 0x40> cond{};
    store<std::uint32_t>(cond.data(), kCondExpect, 0x7f8);
    store<std::uint8_t>(cond.data(), kCondValue, 1);
    g_calls = 0;
    detour_h26_ec_event_flag_state(cond.data(), nullptr, nullptr);
    EXPECT_EQ(load<std::uint8_t>(cond.data(), kCondResult) & 1, 0);
    state().event_flag_7f8_state = true;
    detour_h26_ec_event_flag_state(cond.data(), nullptr, nullptr);
    EXPECT_EQ(load<std::uint8_t>(cond.data(), kCondResult) & 1, 1);
    EXPECT_EQ(g_calls, 0);
    store<std::uint32_t>(cond.data(), kCondExpect, 0x100);
    EXPECT_EQ(detour_h26_ec_event_flag_state(cond.data(), nullptr, nullptr), 9u);
    EXPECT_EQ(g_calls, 1);
}

TEST_F(WorldTest, H27CapsMemberCountAtThreeUnlessPreference) {
    std::array<std::byte, 0x40> cond{};
    store<std::uint32_t>(cond.data(), kCondValue, 1);
    detour_h27_ec_sortie_member_num(cond.data(), nullptr, nullptr);  // no vector: count 1
    EXPECT_EQ(load<std::uint8_t>(cond.data(), kCondResult) & 1, 1);
    MemberVector members(6);
    state().member_vector_owner = [&] { return static_cast<const void*>(members.owner.data()); };
    store<std::uint32_t>(cond.data(), kCondValue, 3);
    detour_h27_ec_sortie_member_num(cond.data(), nullptr, nullptr);
    EXPECT_EQ(load<std::uint8_t>(cond.data(), kCondResult) & 1, 1);  // 6 capped to 3
    store<std::uint32_t>(cond.data(), kCondValue, 6);
    detour_h27_ec_sortie_member_num(cond.data(), nullptr, nullptr);
    EXPECT_EQ(load<std::uint8_t>(cond.data(), kCondResult) & 1, 0);
    state().extended_party_preference = [] { return true; };
    detour_h27_ec_sortie_member_num(cond.data(), nullptr, nullptr);
    EXPECT_EQ(load<std::uint8_t>(cond.data(), kCondResult) & 1, 1);
    MemberVector two(2);
    state().member_vector_owner = [&] { return static_cast<const void*>(two.owner.data()); };
    store<std::uint32_t>(cond.data(), kCondValue, 2);
    detour_h27_ec_sortie_member_num(cond.data(), nullptr, nullptr);
    EXPECT_EQ(load<std::uint8_t>(cond.data(), kCondResult) & 1, 1);
}

// ---------------------------------------------------------------- HookSpecs
namespace {
std::vector<HookSpec> specs_for_fake_table() {
    return build_world_specs([](unsigned off) { return std::uint64_t{0x140000000} + off * 0x1000ull; });
}
const HookSpec& find(const std::vector<HookSpec>& v, std::string_view prefix) {
    for (const auto& s : v)
        if (std::string_view(s.name).substr(0, prefix.size()) == prefix) return s;
    throw std::runtime_error("no spec " + std::string(prefix));
}
}  // namespace

TEST_F(WorldTest, SpecCountAndNamesUnique) {
    const auto v = specs_for_fake_table();
    EXPECT_EQ(v.size(), 23u);  // 11 game-installer hooks + 12 protected-installed entries
    std::map<std::string, int> names;
    for (const auto& s : v) EXPECT_EQ(++names[s.name], 1) << s.name;
}

TEST_F(WorldTest, VtableSlotSitesAreVtablePlusSlotOffset) {
    const auto v = specs_for_fake_table();
    auto at = [](unsigned off) { return std::uintptr_t{0x140000000} + off * 0x1000ull; };
    EXPECT_EQ(find(v, "hook#1 ").site, at(0x228) + 0x58);
    EXPECT_EQ(find(v, "hook#17").site, at(0x200) + 0x58);
    EXPECT_EQ(find(v, "hook#20").site, at(0x210) + 0x20);
    EXPECT_EQ(find(v, "hook#21").site, at(0x210) + 0x28);
    EXPECT_EQ(find(v, "hook#22").site, at(0x210) + 0x68);
    EXPECT_EQ(find(v, "hook#23").site, at(0x248) + 8);
    EXPECT_EQ(find(v, "hook#24").site, at(0x250) + 8);
    EXPECT_EQ(find(v, "hook#25").site, at(0x258) + 8);
    EXPECT_EQ(find(v, "hook#26").site, at(0x260) + 8);
    EXPECT_EQ(find(v, "hook#27").site, at(0x268) + 8);
    for (const char* n : {"hook#1 ", "hook#17", "hook#20", "hook#21", "hook#22", "hook#23", "hook#24", "hook#25", "hook#26", "hook#27"})
        EXPECT_EQ(find(v, n).kind, Kind::VtableSlot) << n;
}

TEST_F(WorldTest, KindsOfProtectedAndInlineHooks) {
    const auto v = specs_for_fake_table();
    EXPECT_EQ(find(v, "hook#6").kind, Kind::Inline);
    EXPECT_EQ(find(v, "P0 ").kind, Kind::CallSite);
    EXPECT_EQ(find(v, "P5 ").kind, Kind::Inline);
    for (const char* n : {"P6 ", "P7 ", "P8 ", "P12 ", "P14 ", "P15 "}) EXPECT_EQ(find(v, n).kind, Kind::CallSite) << n;
    for (const char* n : {"P10 ", "P11 ", "P13 "}) EXPECT_EQ(find(v, n).kind, Kind::JmpEntry) << n;
    for (const char* n : {"P0 ", "P6 ", "P7 ", "P8 ", "P12 ", "P14 ", "P15 "})
        EXPECT_EQ(find(v, n).expect, std::vector<std::uint8_t>{0xE8}) << n;
}

TEST_F(WorldTest, P16IsOneBytePatchWithExpect) {
    const auto v = specs_for_fake_table();
    const auto& p16 = find(v, "P16");
    EXPECT_EQ(p16.kind, Kind::BytePatch);
    EXPECT_EQ(p16.site, std::uintptr_t{0x140000000} + 0x5d0 * 0x1000ull);
    EXPECT_EQ(p16.bytes, std::vector<std::uint8_t>{0x2f});
    EXPECT_EQ(p16.expect, std::vector<std::uint8_t>{0x11});
    EXPECT_EQ(p16.detour, nullptr);
}

TEST_F(WorldTest, DetoursAndOriginalSlotsAreWired) {
    const auto v = specs_for_fake_table();
    for (const auto& s : v) {
        if (s.kind != Kind::BytePatch) {
            EXPECT_NE(s.detour, nullptr) << s.name;
        }
    }
    EXPECT_EQ(find(v, "hook#17").detour, reinterpret_cast<void*>(&detour_h17_auto_control_register));
    EXPECT_EQ(find(v, "hook#17").original, reinterpret_cast<void**>(&state().orig.auto_control_register));
    EXPECT_EQ(find(v, "P5 ").original, reinterpret_cast<void**>(&state().orig.p5_battle_action));
    EXPECT_EQ(find(v, "hook#21").original, nullptr);
    EXPECT_EQ(find(v, "P10 ").original, nullptr);
    // The installer writes through the slot: detours see the original afterwards.
    *find(v, "hook#24").original = reinterpret_cast<void*>(&fake_ec_pass);
    g_calls = 0;
    EXPECT_EQ(detour_h24_ec_chr_dead(nullptr, nullptr, nullptr), 7u);
}

TEST_F(WorldTest, BuildRecordsPartyMemberInfoVtable) {
    specs_for_fake_table();
    EXPECT_EQ(state().party_member_info_vtable, std::uintptr_t{0x140000000} + 0x208 * 0x1000ull);
}

TEST_F(WorldTest, GroupWorldUsesTheSignatureTable) {
    const signatures::SignatureTable table;  // unresolved: every slot reads 0
    const auto v = groups::world(table, config::Settings{});
    EXPECT_EQ(v.size(), 23u);  // 11 game-installer hooks + 12 protected-installed entries
    EXPECT_EQ(find(v, "hook#20").site, 0x20u);
    EXPECT_EQ(find(v, "P16").site, 0u);
}
