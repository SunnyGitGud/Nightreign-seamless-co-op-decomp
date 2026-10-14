#pragma once
// Raw access to game structures through documented byte offsets (docs/hooks.md section 9,
// evidence/hooks/detours/group1..4.md). The detours never name game types: they read and write fields at offsets.
#include <cstddef>
#include <cstdint>

#include "nrsc/core/raw_memory.hpp"

namespace nrsc::hooks::world::layout {

using raw::load;
using raw::load_ptr;
using raw::store;

// --- CS::PartyMemberInfo (ctor 0x140aec960; nrsc layout from the P10 detour 0x180026c50, grade C) ------------------
inline constexpr std::size_t kPartyInfoSize = 0x180;      // the game allocates 0xf0 (3 slots)
inline constexpr std::size_t kPartyInfoSlots = 6;
inline constexpr std::size_t kPartyInfoFirstSlot = 0x28;  // 0x30-byte member slots at 0x28 + 0x30 * i
inline constexpr std::size_t kPartyInfoSlotStride = 0x30;
inline constexpr std::size_t kPartyInfoTail = 0x148;      // tail fields 0x148..0x17f

// --- CSSessionManager (FD4Singleton 0x143c21ec8; group2.md, group1.md section 12, grade S) ---------------------------
inline constexpr std::size_t kSessionState = 0x0c;        // u32; 3 = "connected" (meaning W)
inline constexpr std::size_t kSessionMode = 0x10;         // u32; 5 is tested by the PartyMemberInfo reset
inline constexpr std::size_t kSessionPlayersBegin = 0x78; // std::vector of 0xf0-byte entries: begin at +0x78, end at +0x80
inline constexpr std::size_t kSessionPlayersEnd = 0x80;
inline constexpr std::size_t kSessionPlayerStride = 0xf0;
inline constexpr std::size_t kSessionPlayerId = 0xd4;     // u32 inside an entry
inline constexpr std::uint32_t kSessionStateConnected = 3;

// --- Lobby member vector (CSQuickMatchLobbyContext hooks 21 and 27): begin +0x30, end +0x38, 0xf0-byte records ----
inline constexpr std::size_t kMemberVectorBegin = 0x30;
inline constexpr std::size_t kMemberVectorEnd = 0x38;
inline constexpr std::size_t kMemberRecordSize = 0xf0;  // erratum in docs/hooks.md section 9: 0xf0, not 0x78

// --- Ec* condition objects (hooks 23-27) ---------------------------------------------------------------------------
inline constexpr std::size_t kCondResult = 0x18;   // bit 0 is the result
inline constexpr std::size_t kCondValue = 0x28;    // u32
inline constexpr std::size_t kCondExpect = 0x2c;   // byte (PlayerNo, SortieMemberNum), mode byte (TeamType), flag id u32 (EventFlagState)
inline constexpr std::size_t kCondSize = 0x40;     // the EcTestTeamType retry copies this many bytes
inline constexpr std::size_t kEcCtxScriptId = 0x30;  // a3->+0x30: id of the evaluating script/event (W)

// --- GameDataMan-like global at 0x143c078d0 (0x458 bytes, name W) -----------------------------------------------------
inline constexpr std::size_t kGlobalLocalPlayerHolder = 0x08;  // [G+8] -> object whose +8 is the local player number
inline constexpr std::size_t kGlobalRecordArray = 0x10;        // [G+0x10] -> array of 0x8a0-byte player records
inline constexpr std::size_t kGlobalFlagArray = 0x20;          // [G+0x20] -> 2-byte flag array (the game allocates 2 bytes)
inline constexpr std::size_t kPlayerRecordStride = 0x8a0;
inline constexpr std::size_t kPlayerRecordId = 0x08;
inline constexpr std::size_t kPlayerRecordsChecked = 5;

}  // namespace nrsc::hooks::world::layout
