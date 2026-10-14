#include <cstdint>

#include "nrsc/hooks/groups.hpp"
#include "nrsc/hooks/world/detours.hpp"
#include "nrsc/hooks/world/world_specs.hpp"
#include "nrsc/hooks/world/world_state.hpp"

namespace nrsc::hooks::world {

namespace {

template <class F>
void* fn(F* f) { return reinterpret_cast<void*>(f); }

template <class F>
void** slot(F** p) { return reinterpret_cast<void**>(p); }

HookSpec make(std::string name, Kind kind, std::uint64_t site, void* detour, void** original,
              std::vector<std::uint8_t> expect = {}) {
    HookSpec s;
    s.name = std::move(name);
    s.kind = kind;
    s.site = static_cast<std::uintptr_t>(site);
    s.detour = detour;
    s.original = original;
    s.expect = std::move(expect);
    return s;
}

const std::vector<std::uint8_t> kCallOpcode{0xE8};

}  // namespace

std::vector<HookSpec> build_world_specs(const SiteLookup& at) {
    auto& st = state();
    st.party_member_info_vtable = static_cast<std::uintptr_t>(at(sig::kPartyMemberInfoVtable));
    auto& o = st.orig;
    std::vector<HookSpec> v;

    // hooks.json #1, #6, #17 (installer order).
    v.push_back(make("hook#1 DLRuntimeClassImpl<CSLuaEventScriptImitation>+0x58", Kind::VtableSlot,
                     at(sig::kLuaEventScriptVtable) + kDlrfRegisterSlot, fn(&detour_h1_lua_register), slot(&o.lua_register)));
    v.push_back(make("hook#6 SignIn auto-call (0x140df9420)", Kind::Inline, at(sig::kSignInTrigger),
                     fn(&detour_h6_sign_in), slot(&o.sign_in_trigger)));
    v.push_back(make("hook#17 DLRuntimeClassImpl<CSAutoControlAPI>+0x58", Kind::VtableSlot,
                     at(sig::kAutoControlApiVtable) + kDlrfRegisterSlot, fn(&detour_h17_auto_control_register),
                     slot(&o.auto_control_register)));
    // hooks.json #20-#22: CSQuickMatchLobbyContext.
    v.push_back(make("hook#20 CSQuickMatchLobbyContext+0x20", Kind::VtableSlot,
                     at(sig::kQuickMatchLobbyVtable) + kLobbyUpdateStateSlot, fn(&detour_h20_lobby_update_state),
                     slot(&o.lobby_update_state)));
    v.push_back(make("hook#21 CSQuickMatchLobbyContext+0x28", Kind::VtableSlot,
                     at(sig::kQuickMatchLobbyVtable) + kLobbyMemberCountSlot, fn(&detour_h21_lobby_member_count), nullptr));
    v.push_back(make("hook#22 CSQuickMatchLobbyContext+0x68", Kind::VtableSlot,
                     at(sig::kQuickMatchLobbyVtable) + kLobbyFlagSlot, fn(&detour_h22_lobby_flag), nullptr));
    // hooks.json #23-#27: Ec* conditions.
    v.push_back(make("hook#23 EcTestPlayerNo+0x8", Kind::VtableSlot, at(sig::kEcPlayerNo) + kEcEvaluateSlot,
                     fn(&detour_h23_ec_player_no), nullptr));
    v.push_back(make("hook#24 EcTestChrDead+0x8", Kind::VtableSlot, at(sig::kEcChrDead) + kEcEvaluateSlot,
                     fn(&detour_h24_ec_chr_dead), slot(&o.ec_chr_dead)));
    v.push_back(make("hook#25 EcTestTeamType+0x8", Kind::VtableSlot, at(sig::kEcTeamType) + kEcEvaluateSlot,
                     fn(&detour_h25_ec_team_type), slot(&o.ec_team_type)));
    v.push_back(make("hook#26 EcTestEventFlagState+0x8", Kind::VtableSlot, at(sig::kEcEventFlagState) + kEcEvaluateSlot,
                     fn(&detour_h26_ec_event_flag_state), slot(&o.ec_event_flag_state)));
    v.push_back(make("hook#27 EcTestSortieMemberNum+0x8", Kind::VtableSlot, at(sig::kEcSortieMemberNum) + kEcEvaluateSlot,
                     fn(&detour_h27_ec_sortie_member_num), nullptr));

    // Protected-installed hooks (devirt/out/v2/0x1800221e0.hooks_table.txt). Function-entry bytes are the on-disk
    // bytes of nightreign.exe v1.03.3 (C for that version).
    v.push_back(make("P0 ctor of the 0x458-byte global (call 0x1401fa740)", Kind::CallSite, at(sig::kP0),
                     fn(&detour_p0_game_data_ctor), slot(&o.p0_game_data_ctor), kCallOpcode));
    v.push_back(make("P5 0x1405769d0", Kind::Inline, at(sig::kP5), fn(&detour_p5_battle_action),
                     slot(&o.p5_battle_action)));
    v.push_back(make("P6 handle to chr pair (call 0x1405f7fb0)", Kind::CallSite, at(sig::kP6),
                     fn(&detour_p6_handle_to_chr), nullptr, kCallOpcode));
    v.push_back(make("P7 local-or-owned (call 0x1403d3fe0)", Kind::CallSite, at(sig::kP7),
                     fn(&detour_p7_is_local_or_owned), nullptr, kCallOpcode));
    v.push_back(make("P8 CSSessionManager contains (call 0x140df0af0)", Kind::CallSite, at(sig::kP8),
                     fn(&detour_p8_session_contains), slot(&o.p8_session_contains), kCallOpcode));
    v.push_back(make("P10 CS::PartyMemberInfo ctor (6 slots)", Kind::JmpEntry, at(sig::kP10),
                     fn(&detour_p10_party_member_info_ctor), nullptr,
                     {0x48, 0x89, 0x5C, 0x24, 0x10, 0x48, 0x89, 0x4C, 0x24, 0x08}));
    v.push_back(make("P11 CS::PartyMemberInfo reset", Kind::JmpEntry, at(sig::kP11),
                     fn(&detour_p11_party_member_info_reset), nullptr,
                     {0x48, 0x89, 0x5C, 0x24, 0x08, 0x57, 0x48, 0x83, 0xEC, 0x20, 0x8B, 0x41, 0x28}));
    v.push_back(make("P12 bloodstain_man ctor (call 0x1407286d0)", Kind::CallSite, at(sig::kP12),
                     fn(&detour_p12_bloodstain_man_ctor), nullptr, kCallOpcode));
    v.push_back(make("P13 per-player flag 0x1401fecb0", Kind::JmpEntry, at(sig::kP13), fn(&detour_p13_player_flag),
                     nullptr, {0x4C, 0x8B, 0x0D}));
    v.push_back(make("P14 ChrIns action (call 0x1403df9b0)", Kind::CallSite, at(sig::kP14),
                     fn(&detour_p14_chr_action), slot(&o.p14_chr_action), kCallOpcode));
    v.push_back(make("P15 effect handle IsValid (call 0x140ed6740)", Kind::CallSite, at(sig::kP15),
                     fn(&detour_p15_effect_handle_valid), nullptr, kCallOpcode));

    // P16: the slot-0x5d0 site is the imm8 of `cmp eax,0x11` in AtqSpEffectEquipCtrl slot 11 (0x1404ff6f7), a live
    // 1-byte patch 0x11 -> 0x2f (3 -> 8 groups of 6). Not a detour (C, group2.md).
    HookSpec p16;
    p16.name = "P16 AtqSpEffectEquipCtrl slot range (cmp eax,0x11 -> 0x2f)";
    p16.kind = Kind::BytePatch;
    p16.site = static_cast<std::uintptr_t>(at(sig::kP16));
    p16.bytes = {0x2F};
    p16.expect = {0x11};
    v.push_back(std::move(p16));
    return v;
}

}  // namespace nrsc::hooks::world

namespace nrsc::hooks::groups {

std::vector<HookSpec> world(const signatures::SignatureTable& table, const config::Settings& /*settings*/) {
    // The world group has no settings-dependent behaviour (the preferences feed the params group).
    return world::build_world_specs([&](unsigned offset) { return table.at(offset); });
}

}  // namespace nrsc::hooks::groups
