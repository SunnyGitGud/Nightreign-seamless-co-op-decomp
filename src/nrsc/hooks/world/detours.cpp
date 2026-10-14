#include "nrsc/hooks/world/detours.hpp"

#include <algorithm>
#include <cstring>
#include <mutex>
#include <new>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/hooks/world/game_layout.hpp"
#include "nrsc/hooks/world/world_state.hpp"

namespace nrsc::hooks::world {

using namespace layout;

namespace {

constexpr const char* kAutoControlSource = "nrsc\\cs\\auto_control_api\\auto_control_api.cpp";

const void* current_session() {
    const auto& s = state();
    return s.session_manager ? s.session_manager() : nullptr;
}

bool mod_active() { return (state().mod_flags.load() & kModeActiveBit) != 0; }

// "session state == 3" shared by P7 and P8 (group1.md section 12, group2.md): the session manager exists and +0xc is 3.
bool session_connected() {
    const void* sess = current_session();
    return sess != nullptr && load<std::uint32_t>(sess, kSessionState) == kSessionStateConnected;
}

bool preference_on() {
    const auto& s = state();
    return s.extended_party_preference && s.extended_party_preference();
}

// Slot reset used by P11 (0x180026e50, C): -1, 1, 0, dword 0, 16 bytes {-1,0,-1,0}, dword 0.
void init_member_slot(void* obj, std::size_t base) {
    store<std::uint64_t>(obj, base + 0x00, ~0ull);
    store<std::uint64_t>(obj, base + 0x08, 1);
    store<std::uint64_t>(obj, base + 0x10, 0);
    store<std::uint32_t>(obj, base + 0x18, 0);
    store<std::uint32_t>(obj, base + 0x1c, ~0u);
    store<std::uint32_t>(obj, base + 0x20, 0);
    store<std::uint32_t>(obj, base + 0x24, ~0u);
    store<std::uint32_t>(obj, base + 0x28, 0);
    store<std::uint32_t>(obj, base + 0x2c, 0);
}

}  // namespace

// === P0 ======================================================================================================
// Call-site detour (E8 at 0x1401fd5fc) of the ctor 0x1401fa740 of the 0x458-byte global at 0x143c078d0 (identity W).
// Behaviour C (group1.md section 5), purpose W.
void* detour_p0_game_data_ctor(void* mem) {
    auto& s = state();
    s.player_flags.fill(0);  // ctx[0x78]: u8 +0x3c = 0 and u32 +0x38 = 0 (no reader in readable code)
    auto* original = s.orig.p0_game_data_ctor;
    void* object = original ? original(mem) : mem;  // the original runs first and returns `this`
    if (object) {
        // The game allocates this 2-byte array with allocator vtbl[0x50](2, 1); the detour clears 5 bytes (C, anomaly
        // noted in group1.md: harmless only if the allocator rounds up, W).
        void* flags = const_cast<void*>(load_ptr(object, kGlobalFlagArray));
        if (flags) {
            store<std::uint32_t>(flags, 0, 0);
            store<std::uint8_t>(flags, 4, 0);
        }
    }
    return object;
}

// === P5 ======================================================================================================
// MinHook on 0x1405769d0 (call from CheckIsAllDead's neighbourhood). Gate + lock around the original. Behaviour C
// (group1.md section 10), purpose and function name W.
std::uint32_t detour_p5_battle_action(void* a0, void* a1, std::int32_t a2, std::int32_t a3, std::uint8_t a4,
                                      std::uint8_t a5, std::uint8_t a6, std::uint64_t a7) {
    auto& s = state();
    const std::uint32_t mode = s.mode_word.load();
    if (!(mode & kModeWordBit0)) {
        if (a2 == -1) return 0;
    } else {
        if (a2 == -1 || !(mode & kModeWordBit2)) return 0;  // bit 0 set requires bit 2 as well
    }
    std::lock_guard<std::recursive_mutex> guard(s.p5_lock);
    const std::int32_t target = a1 ? load<std::int32_t>(a1, 8) : -1;
    s.current_target.store(target);  // published for the sibling hooks while the original runs
    const auto original = s.orig.p5_battle_action;
    const std::uint32_t result = original ? original(a0, a1, a2, a3, a4, a5, a6, a7) : 0;
    s.secondary_target.store(-1);
    s.current_target.store(-1);
    return result;
}

// === P6 ======================================================================================================
// Call-site detour of 0x1405f7fb0 (handle -> {chr_a, chr_b}). No original. Behaviour C (group1.md section 11): the
// output slot follows the handle type (8 -> first, 6 -> second) and the game's chr+0x190 test is not applied.
void* detour_p6_handle_to_chr(void* out, std::uint32_t* handle) {
    auto& s = state();
    store<void*>(out, 0, nullptr);
    store<void*>(out, 8, nullptr);
    const std::uint32_t h = *handle;
    if (h == 0xffffffffu) return out;
    void* manager = s.chr_manager ? s.chr_manager() : nullptr;
    if (!manager || !s.find_chr_by_handle) return out;
    std::uint32_t copy = h;
    switch (h >> 28) {
        case 8:
            if (void* chr = s.find_chr_by_handle(manager, &copy)) store<void*>(out, 0, chr);
            break;
        case 6:
            if (void* chr = s.find_chr_by_handle(manager, &copy)) store<void*>(out, 8, chr);
            break;
        default:
            break;
    }
    return out;
}

// === P7 ======================================================================================================
// Call-site detour of 0x1403d3fe0 "local player or owned". Behaviour C (group1.md section 12), purpose W.
bool detour_p7_is_local_or_owned(void* /*chr*/) {
    if (!mod_active()) return true;  // flag clear: always true (the game's block runs for every chr)
    return session_connected();
}

// === P8 ======================================================================================================
// Call-site detour of 0x140df0af0 inside CS::CSBattleGroup::CheckIsAllDead. Behaviour C, hooked function S
// (group2.md). With the flag clear the original runs; with it set "session state == 3" replaces the map lookup.
bool detour_p8_session_contains(void* mgr, void* key) {
    if (!mod_active()) {
        const auto original = state().orig.p8_session_contains;
        return original ? original(mgr, key) : false;
    }
    return session_connected();
}

// === P10 =====================================================================================================
// Entry jump (E9) over CS::PartyMemberInfo::PartyMemberInfo (0x140aec960): replaces the 0xf0-byte, 3-slot object by a
// 0x180-byte, 6-slot one: party 3 -> 6. The original is not called; the game's buffer is ignored. Behaviour C
// (nrsc 0x180026c50..0x180026e48), purpose S. nrsc uses its own CRT operator new here (allocator mismatch with the
// game's free, W).
void* detour_p10_party_member_info_ctor(void* /*game_buffer*/) {
    auto& s = state();
    auto* obj = static_cast<std::byte*>(::operator new(kPartyInfoSize));
    std::memset(obj, 0, kPartyInfoSize);
    store<std::uintptr_t>(obj, 0, s.party_member_info_vtable);
    for (std::size_t i = 0; i < kPartyInfoSlots; ++i) init_member_slot(obj, kPartyInfoFirstSlot + kPartyInfoSlotStride * i);
    // Tail fields 0x148..0x17f (C): 16 bytes -1, 16 bytes 0, qword -1, dword 0, byte 0, dword -1, word 0.
    std::memset(obj + 0x148, 0xff, 16);
    std::memset(obj + 0x158, 0x00, 16);
    store<std::uint64_t>(obj, 0x168, ~0ull);
    store<std::uint32_t>(obj, 0x170, 0);
    store<std::uint8_t>(obj, 0x174, 0);
    store<std::uint32_t>(obj, 0x178, ~0u);
    store<std::uint16_t>(obj, 0x17c, 0);
    s.party_member_info = obj;  // P[2]
    return obj;
}

// === P11 =====================================================================================================
// Entry jump over the PartyMemberInfo reset (0x140aee1e0). Behaviour C (nrsc 0x180026e50..0x180027082), purpose S;
// the meaning of session mode 5 and record state 2 is U. The tail call goes to ctx[0x80]+0x18 which no HookSpec
// fills (JmpEntry has no trampoline), so the default is "no original" (U).
void detour_p11_party_member_info_reset(void* pmi) {
    auto& s = state();
    const void* sess = current_session();
    const std::uint32_t mode = sess ? load<std::uint32_t>(sess, kSessionMode) : 0;
    for (std::size_t i = 0; i < kPartyInfoSlots; ++i) {
        const std::size_t base = kPartyInfoFirstSlot + kPartyInfoSlotStride * i;
        if (load<std::uint64_t>(pmi, base) != ~0ull) init_member_slot(pmi, base);
    }
    std::memset(static_cast<std::byte*>(pmi) + 0x148, 0xff, 16);
    store<std::uint32_t>(pmi, 0x158, 0);
    if (mode == 5 && load<std::uint32_t>(pmi, 0x160) != 0 && load<std::uint32_t>(pmi, 0x15c) == 2) return;
    store<std::uint32_t>(pmi, 0x15c, 0);
    store<std::uint32_t>(pmi, 0x164, 0);  // constant at 0x180145d90: {0, -1, -1, 0}
    store<std::uint32_t>(pmi, 0x168, ~0u);
    store<std::uint32_t>(pmi, 0x16c, ~0u);
    store<std::uint32_t>(pmi, 0x170, 0);
    store<std::uint8_t>(pmi, 0x174, 0);
    store<std::uint32_t>(pmi, 0x178, ~0u);
    if (s.orig.p11_party_info_reset) s.orig.p11_party_info_reset(pmi);
}

// === P12 =====================================================================================================
// Call-site detour of the bloodstain-manager ctor 0x1407286d0 (0x68 bytes, 3 slots) in InGameStep::_Common_Initialize:
// 0x78-byte, 6-slot block. Behaviour C (docs/world_chr_man.md section 3; src/nrsc/cs/bloodstain_man).
void* detour_p12_bloodstain_man_ctor(void* block) {
    const auto& allocator_for = state().bloodstain_allocator_for;
    const cs::AllocatorFor lookup = [&](void* arg) -> const cs::BloodstainAllocator* {
        return allocator_for ? allocator_for(arg) : nullptr;
    };
    return cs::create_bloodstain_block(lookup, block);
}

// === P13 =====================================================================================================
// Entry jump over 0x1401fecb0 `bool f(playerId)` on the global 0x143c078d0. Behaviour C (group2.md), purpose W:
// searches 5 records (stride 0x8a0, id at +8) instead of 2 and answers from nrsc's flag array.
bool detour_p13_player_flag(std::uint32_t player_id) {
    auto& s = state();
    const void* global = s.game_data_man ? s.game_data_man() : nullptr;
    if (!global) return false;
    const void* records = load_ptr(global, kGlobalRecordArray);
    if (!records) return false;
    for (std::size_t i = 0; i < kPlayerRecordsChecked; ++i)
        if (load<std::uint32_t>(records, i * kPlayerRecordStride + kPlayerRecordId) == player_id)
            return s.player_flags[i] != 0;
    return false;
}

// === P14 =====================================================================================================
// Call-site detour of 0x1403df9b0 (ChrIns-family action). Repeats the original for every other player by shifting
// the handle at obj+8 by id * 100. Behaviour C, purpose W (group2.md).
void detour_p14_chr_action(void* obj) {
    auto& s = state();
    std::vector<std::uint32_t> ids;
    if (!s.use_fixed_player_ids) {
        if (const void* sess = current_session()) {
            const auto* begin = static_cast<const std::byte*>(load_ptr(sess, kSessionPlayersBegin));
            const auto* end = static_cast<const std::byte*>(load_ptr(sess, kSessionPlayersEnd));
            for (const std::byte* e = begin; begin && e + kSessionPlayerStride <= end; e += kSessionPlayerStride)
                ids.push_back(load<std::uint32_t>(e, kSessionPlayerId));
        }
    } else {
        ids = {1, 2, 3, 4, 5, 6};
    }
    const auto original = s.orig.p14_chr_action;
    if (!original) return;
    original(obj);
    const std::uint64_t handle = load<std::uint64_t>(obj, 8);
    for (const std::uint32_t id : ids) {
        store<std::uint64_t>(obj, 8, handle + static_cast<std::uint64_t>(id) * 100);
        original(obj);
    }
    store<std::uint64_t>(obj, 8, handle);
}

// === P15 =====================================================================================================
// Call-site detour of the effect-handle IsValid 0x140ed6740. Same predicate as the game's, on the pointer at h+0x18
// instead of h+0 (unexplained). Behaviour C, purpose U (group2.md).
bool detour_p15_effect_handle_valid(void* handle) {
    const void* p = load_ptr(handle, 0x18);
    return p != nullptr && load<std::uint32_t>(p, 0x10) != 3;
}

// === P16 helper ==============================================================================================
// Not reached from the 0x1404ff6f7 site (live: 1-byte patch). min(pref ? 6 : 3, cap), 1 when the owner is absent.
// Behaviour C, reachability U (group2.md).
std::uint32_t p16_slot_group_size(const void* a1) {
    const std::uint32_t n = preference_on() ? 6 : 3;
    const void* owner = a1 ? load_ptr(a1, 0) : nullptr;
    const void* inner = owner ? load_ptr(owner, 0xa8) : nullptr;
    if (!inner) return 1;
    return std::min(n, load<std::uint32_t>(inner, 0x98));
}

// === hooks.json #1 ===========================================================================================
// VtableSlot DLRuntimeClassImpl<CSLuaEventScriptImitation>+0x58. Pure pass-through that captures the original (S).
std::uint64_t detour_h1_lua_register(void* self, void* a2, const char* name, std::uint64_t a4) {
    const auto original = state().orig.lua_register;
    return original ? original(self, a2, name, a4) : 0;
}

// === hooks.json #6 ===========================================================================================
// MinHook on 0x140df9420. Runs before the original (group3.md, world_chr_man.md section 4, C): registers the four
// tasks, then once the game is up calls the auto-control method "SignIn"(0); a missing method is fatal. The
// anti-piracy notice is deliberately omitted (docs/plan_gaps.md). The original is tail-called.
bool detour_h6_sign_in(void* a, void* b) {
    auto& s = state();
    for (const auto& task : s.auto_control_tasks)
        if (task.is_registered && !task.is_registered() && task.do_register) task.do_register();
    if (s.sign_in_gate) {
        const SignInGate g = s.sign_in_gate();
        if (cs::should_sign_in(g.game_present, g.game_field_174e8_set, g.sign_in_present, g.sign_in_state)) {
            if (s.sign_in_prepare) s.sign_in_prepare(1);
            cs::AutoControlMethods methods;
            const auto it = s.registered_methods.find(std::string(cs::kSignInMethod));
            if (it != s.registered_methods.end()) {
                void* entry = it->second;
                methods[std::string(cs::kSignInMethod)] = [entry](int arg) {
                    if (auto& call = state().call_registered_method) call(entry, arg);
                };
            }
            try {
                cs::invoke_method(methods, cs::kSignInMethod);
            } catch (const FatalError& e) {
                if (!s.on_fatal) throw;
                s.on_fatal(e.what(), kAutoControlSource, e.line());
            }
        }
    }
    const auto original = s.orig.sign_in_trigger;
    return original ? original(a, b) : false;
}

// === hooks.json #17 ==========================================================================================
// VtableSlot DLRuntimeClassImpl<CSAutoControlAPI>+0x58 (RegisterMethod(this, entry, name, a4), S): copies the method
// name into nrsc's map (`*(entry+8)` as value, meaning U), then always calls the original. Behaviour C, purpose S.
std::uint64_t detour_h17_auto_control_register(void* self, void* entry, const char* name, std::uint64_t a4) {
    auto& s = state();
    if (name && entry) s.registered_methods.try_emplace(std::string(name), const_cast<void*>(load_ptr(entry, 8)));
    const auto original = s.orig.auto_control_register;
    return original ? original(self, entry, name, a4) : 0;
}

// === hooks.json #20-#22 : CSQuickMatchLobbyContext ===========================================================
// #20 (vtbl+0x20, UpdateState): forces mode (+0x10) = 2, then the original. Behaviour C, purpose W.
void detour_h20_lobby_update_state(void* self) {
    if (load<std::uint32_t>(self, 0x10) != 2) store<std::uint32_t>(self, 0x10, 2);
    if (const auto original = state().orig.lobby_update_state) original(self);
}

std::uint32_t lobby_member_count() {
    const auto& s = state();
    const void* owner = s.member_vector_owner ? s.member_vector_owner() : nullptr;
    if (!owner) return 0;
    const auto begin = load<std::uintptr_t>(owner, kMemberVectorBegin);
    const auto end = load<std::uintptr_t>(owner, kMemberVectorEnd);
    if (end < begin) return 0;
    return static_cast<std::uint32_t>((end - begin) / kMemberRecordSize);  // magic 0x8888888888888889 = /240 (C)
}

// #21 (vtbl+0x28, setter of +0x98): the member count of the real vector replaces the value; original not called.
// Behaviour C (stride 0xf0 per the erratum), purpose S.
void detour_h21_lobby_member_count(void* self, std::uint32_t value) {
    const std::uint32_t n = lobby_member_count();
    store<std::uint32_t>(self, 0x98, n ? n : value);
}

// #22 (vtbl+0x68, setter of byte +0xc0): mirrored as bit 6 of nrsc's word; original not called. Behaviour C, purpose W.
void detour_h22_lobby_flag(void* self, std::uint8_t flag) {
    auto& w = state().quick_match_flags;
    w = (w & ~0x40ull) | (static_cast<std::uint64_t>(flag) << 6);
    store<std::uint8_t>(self, 0xc0, flag);
}

// === hooks.json #23-#27 : Ec* conditions =====================================================================
// #23 EcTestPlayerNo (vtbl+0x8): local player number, folded mod 3 for listed script ids unless the preference is on.
// Original not called. Behaviour C, purpose S.
void detour_h23_ec_player_no(void* cond, void* /*a2*/, void* a3) {
    auto& s = state();
    std::uint32_t number = 0;
    const void* global = s.game_data_man ? s.game_data_man() : nullptr;
    const void* holder = global ? load_ptr(global, kGlobalLocalPlayerHolder) : nullptr;
    if (holder) {
        number = load<std::uint32_t>(holder, 8);
        const auto id = load<std::int32_t>(a3, kEcCtxScriptId);
        const auto& ids = s.fold_player_number_script_ids;
        if (std::find(ids.begin(), ids.end(), id) != ids.end() && !preference_on()) number %= 3;
    }
    const bool result = number == load<std::uint32_t>(cond, kCondValue);
    const bool expected = load<std::uint8_t>(cond, kCondExpect) != 0;
    auto flags = load<std::uint8_t>(cond, kCondResult);
    flags = static_cast<std::uint8_t>((flags & ~1u) | (result == expected ? 1u : 0u));
    store<std::uint8_t>(cond, kCondResult, flags);
}

// #24 EcTestChrDead: pure pass-through (C).
std::uint64_t detour_h24_ec_chr_dead(void* cond, void* a2, void* a3) {
    const auto original = state().orig.ec_chr_dead;
    return original ? original(cond, a2, a3) : 0;
}

// #25 EcTestTeamType: original first; for script id 0x4a0 in mode 2 and a false result, retries the team types
// 10002..10006. Behaviour C, purpose S.
std::uint64_t detour_h25_ec_team_type(void* cond, void* a2, void* a3) {
    auto& s = state();
    const auto original = s.orig.ec_team_type;
    const std::uint64_t result = original ? original(cond, a2, a3) : 0;
    const auto id = load<std::uint32_t>(a3, kEcCtxScriptId);
    const std::uint8_t mode = load<std::uint8_t>(cond, kCondExpect);
    if (mode == 2) {
        auto& seen = s.team_type_ids_seen;
        if (std::find(seen.begin(), seen.end(), static_cast<std::int32_t>(id)) == seen.end())
            seen.push_back(static_cast<std::int32_t>(id));
    }
    if (original && id == 0x4a0 && mode == 2 && !(load<std::uint8_t>(cond, kCondResult) & 1)) {
        for (std::uint32_t team = 10002; team <= 10006; ++team) {
            std::byte copy[kCondSize];
            std::memcpy(copy, cond, kCondSize);
            store<std::uint32_t>(copy, kCondValue, team);
            original(copy, a2, a3);
            if (load<std::uint8_t>(copy, kCondResult) & 1) {
                store<std::uint8_t>(cond, kCondResult, load<std::uint8_t>(cond, kCondResult) | 1);
                break;
            }
        }
    }
    return result;
}

// #26 EcTestEventFlagState: event flag 0x7f8 with expected state 1 is answered from nrsc's state byte; every other
// flag goes to the original. Behaviour C, purpose W.
std::uint64_t detour_h26_ec_event_flag_state(void* cond, void* a2, void* a3) {
    auto& s = state();
    if (load<std::uint32_t>(cond, kCondExpect) == 0x7f8 && load<std::uint8_t>(cond, kCondValue) == 1) {
        auto flags = load<std::uint8_t>(cond, kCondResult);
        flags = static_cast<std::uint8_t>((flags & ~1u) | (s.event_flag_7f8_state ? 1u : 0u));
        store<std::uint8_t>(cond, kCondResult, flags);
        return 0;
    }
    const auto original = s.orig.ec_event_flag_state;
    return original ? original(cond, a2, a3) : 0;
}

// #27 EcTestSortieMemberNum: member count from the real vector (default 1), capped at 3 unless the preference is
// on; compared with cond+0x28. Original not called. Behaviour C, purpose S.
void detour_h27_ec_sortie_member_num(void* cond, void* /*a2*/, void* /*a3*/) {
    std::uint32_t n = lobby_member_count();
    if (n == 0) n = 1;
    else if (n >= 4 && !preference_on()) n = 3;
    auto flags = load<std::uint8_t>(cond, kCondResult);
    flags = static_cast<std::uint8_t>((flags & ~1u) | (n == load<std::uint32_t>(cond, kCondValue) ? 1u : 0u));
    store<std::uint8_t>(cond, kCondResult, flags);
}

// === state ===================================================================================================
namespace {
std::unique_ptr<WorldState>& instance() {
    static std::unique_ptr<WorldState> s = std::make_unique<WorldState>();
    return s;
}
}  // namespace

WorldState& state() { return *instance(); }
void reset_state() { instance() = std::make_unique<WorldState>(); }

}  // namespace nrsc::hooks::world
