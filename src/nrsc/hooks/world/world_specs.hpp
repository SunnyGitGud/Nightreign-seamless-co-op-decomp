#pragma once
// HookSpec construction for the world group, separated from the signature table so it can be tested with a fake one.
#include <cstdint>
#include <functional>
#include <vector>

#include "nrsc/hooks/hook.hpp"

namespace nrsc::hooks::world {

// Signature table offsets used by this group (docs/signatures.md section 2; hooks.json tgt.table_off and the protected
// installer table devirt/out/v2/0x1800221e0.hooks_table.txt sigtable_off).
namespace sig {
inline constexpr unsigned kPartyMemberInfoVtable = 0x208;  // RTTI CS::PartyMemberInfo (P10's vtable pointer P[1])
inline constexpr unsigned kAutoControlApiVtable = 0x200;   // DLRuntimeClassImpl<CSAutoControlAPI>
inline constexpr unsigned kQuickMatchLobbyVtable = 0x210;  // CS::CSQuickMatchLobbyContext
inline constexpr unsigned kSignInTrigger = 0x128;          // hook #6, mode 1: call target 0x140df9420
inline constexpr unsigned kLuaEventScriptVtable = 0x228;   // DLRuntimeClassImpl<CSLuaEventScriptImitation>
inline constexpr unsigned kEcPlayerNo = 0x248;
inline constexpr unsigned kEcChrDead = 0x250;
inline constexpr unsigned kEcTeamType = 0x258;
inline constexpr unsigned kEcEventFlagState = 0x260;
inline constexpr unsigned kEcSortieMemberNum = 0x268;
inline constexpr unsigned kP0 = 0x278;   // E8 at 0x1401fd5fc
inline constexpr unsigned kP12 = 0x280;  // E8 at 0x140c2b294
inline constexpr unsigned kP5 = 0x2a8;   // mode 1: function 0x1405769d0
inline constexpr unsigned kP6 = 0x2b0;   // E8 at 0x140581ba5
inline constexpr unsigned kP7 = 0x2b8;   // E8 at 0x140576a59
inline constexpr unsigned kP8 = 0x2c0;   // E8 at 0x14037f401
inline constexpr unsigned kP10 = 0x2e0;  // mode 1: function entry 0x140aec960
inline constexpr unsigned kP11 = 0x2e8;  // function entry 0x140aee1e0
inline constexpr unsigned kP13 = 0x2f0;  // mode 1: function entry 0x1401fecb0
inline constexpr unsigned kP14 = 0x358;  // E8 at 0x1403cd37b
inline constexpr unsigned kP15 = 0x388;  // E8 at 0x14072ae17
inline constexpr unsigned kP16 = 0x5d0;  // imm8 of `cmp eax,0x11` at 0x1404ff6f7
}  // namespace sig

// Vtable slot offsets (hooks.json tgt.vtable_slot_off).
inline constexpr std::uintptr_t kDlrfRegisterSlot = 0x58;
inline constexpr std::uintptr_t kLobbyUpdateStateSlot = 0x20;
inline constexpr std::uintptr_t kLobbyMemberCountSlot = 0x28;
inline constexpr std::uintptr_t kLobbyFlagSlot = 0x68;
inline constexpr std::uintptr_t kEcEvaluateSlot = 0x08;

using SiteLookup = std::function<std::uint64_t(unsigned table_offset)>;

// Builds every spec of the group from `at(table_offset)`. Also records the PartyMemberInfo vtable in the world state
// (P10's P[1]). Hooks whose site is 0 are still returned: the installer reports them.
std::vector<HookSpec> build_world_specs(const SiteLookup& at);

}  // namespace nrsc::hooks::world
