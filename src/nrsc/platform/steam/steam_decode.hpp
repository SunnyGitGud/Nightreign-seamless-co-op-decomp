#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

// Platform-independent decoding of the Steamworks POD layouts the adapter touches (64-bit Windows, Steam's
// #pragma pack(push, 8)). Layouts are from the public Steamworks headers (isteammatchmaking.h, steamnetworkingtypes.h,
// isteamnetworkingmessages.h, isteamfriends.h) (S); the offsets the original nrsc.dll itself uses are marked (C) where
// docs/server_manager.md / docs/devirt.md confirm them. Nothing here needs the Steamworks SDK.
namespace nrsc::platform::steam {

using SteamId = std::uint64_t;

// SteamNetworkingIdentity (S): int32 m_eType @0, int32 m_cbSize @4, union (128 bytes) @8. Total 136.
// For a SteamID: type 0x10 (k_ESteamNetworkingIdentityType_SteamID), size 8, the 64-bit id at +8 (C: 0x800000010 qword
// built at 0x18003f0fd, and the message identity read at msg+0x10/+0x18).
inline constexpr std::size_t kIdentitySize = 136;
inline constexpr std::uint32_t kIdentityTypeSteamId = 0x10;
struct alignas(8) SteamNetworkingIdentity {
    std::int32_t type = 0;
    std::int32_t size = 0;
    std::uint8_t data[128] = {};
};
static_assert(sizeof(SteamNetworkingIdentity) == kIdentitySize);

// Builds the identity {0x10, 8, id} (the bytes SetSteamID64 would produce).
SteamNetworkingIdentity make_steamid_identity(SteamId id);
// Returns the SteamID when the identity is a SteamID identity (type 0x10), else nullopt.
std::optional<SteamId> identity_steam_id(const SteamNetworkingIdentity& identity);

// ISteamNetworkingMessage / SteamNetworkingMessage_t (S), offsets on 64-bit:
//   +0 void* m_pData, +8 int m_cbSize, +12 HSteamNetConnection m_conn, +16 SteamNetworkingIdentity m_identityPeer,
//   +152 int64 m_nConnUserData, +160 usec m_usecTimeReceived, +168 int64 m_nMessageNumber, +176 m_pfnFreeData,
//   +184 m_pfnRelease (C: msg+0xb8 is the Release slot), +192 int m_nChannel, +196 int m_nFlags, +200 int64 m_nUserData.
// (C) The poll reads identity type at +0x10, SteamID at +0x18, payload +0, size +8.
inline constexpr std::size_t kMsgOffData = 0, kMsgOffSize = 8, kMsgOffIdentity = 16, kMsgOffRelease = 184,
                             kMsgOffChannel = 192;
struct MessageHeader {
    const void* data = nullptr;
    std::int32_t size = 0;
    std::uint32_t identity_type = 0;
    SteamId steam_id = 0;   // 0 unless identity_type is a SteamID
    std::int32_t channel = 0;
};
// Decodes the head of a SteamNetworkingMessage_t from `msg` (at least kMsgOffChannel + 4 bytes). nullopt if too short.
std::optional<MessageHeader> decode_message_header(std::span<const std::byte> msg);

// ---- callback structs -------------------------------------------------------------------------------------------
// Steamworks callback ids (k_iSteamMatchmakingCallbacks = 500, k_iSteamNetworkingMessagesCallbacks = 1250,
// k_iSteamNetworkingUtilsCallbacks = 1280, k_iSteamFriendsCallbacks = 300 -> 0x14d = 333).
// LobbyCreated_t (513 = 0x201): EResult @0, pad, uint64 lobby @8. size 16. (C: result @0, id @8)
struct LobbyCreatedEvent { std::int32_t result = 0; SteamId lobby_id = 0; };
// LobbyEnter_t (504 = 0x1f8): uint64 lobby @0, uint32 chat permissions @8, bool locked @12, uint32 response @16. size 24.
struct LobbyEnterEvent { SteamId lobby_id = 0; std::uint32_t chat_permissions = 0; bool locked = false;
                         std::uint32_t enter_response = 0; };
// LobbyChatUpdate_t (506 = 0x1fa): lobby @0, user changed @8, user making change @16, uint32 state change @24. size 32.
struct LobbyChatUpdateEvent { SteamId lobby_id = 0; SteamId user_changed = 0; SteamId making_change = 0;
                              std::uint32_t state_change = 0; };
// LobbyDataUpdate_t (505 = 0x1f9): lobby @0, member @8 (== lobby when the lobby itself changed), uint8 success @16. size 24.
struct LobbyDataUpdateEvent { SteamId lobby_id = 0; SteamId member_id = 0; bool success = false; };
// GameLobbyJoinRequested_t (333 = 0x14d): lobby @0, friend @8. size 16. (C: copies 16 bytes {lobby, friend})
struct GameLobbyJoinRequestedEvent { SteamId lobby_id = 0; SteamId friend_id = 0; };
// SteamNetworkingMessagesSessionRequest_t (1251 = 0x4e3): SteamNetworkingIdentity m_identityRemote @0. size 136.
struct SessionRequestEvent { SteamNetworkingIdentity identity; SteamId steam_id = 0; bool is_steam_id = false; };
// SteamNetworkingMessagesSessionFailed_t (1252 = 0x4e4): SteamNetConnectionInfo_t m_info @0, size 696. Decoded:
// m_identityRemote @0, m_eState int @176, m_eEndReason int @180 (S, steamnetworkingtypes.h).
struct SessionFailedEvent { SteamId steam_id = 0; bool is_steam_id = false; std::int32_t state = 0;
                            std::int32_t end_reason = 0; };
// SteamRelayNetworkStatus_t (1281 = 0x501): ESteamNetworkingAvailability m_eAvail @0 (C: only first dword read).
struct RelayNetworkStatusEvent { std::int32_t avail = 0; };

inline constexpr std::size_t kLobbyCreatedSize = 16, kLobbyEnterSize = 24, kLobbyChatUpdateSize = 32,
                             kLobbyDataUpdateSize = 24, kGameLobbyJoinRequestedSize = 16,
                             kSessionRequestSize = 136, kSessionFailedSize = 696, kRelayStatusMinSize = 4;

// Each decoder returns nullopt when the buffer is shorter than the documented size.
std::optional<LobbyCreatedEvent> decode_lobby_created(std::span<const std::byte> b);
std::optional<LobbyEnterEvent> decode_lobby_enter(std::span<const std::byte> b);
std::optional<LobbyChatUpdateEvent> decode_lobby_chat_update(std::span<const std::byte> b);
std::optional<LobbyDataUpdateEvent> decode_lobby_data_update(std::span<const std::byte> b);
std::optional<GameLobbyJoinRequestedEvent> decode_game_lobby_join_requested(std::span<const std::byte> b);
std::optional<SessionRequestEvent> decode_session_request(std::span<const std::byte> b);
std::optional<SessionFailedEvent> decode_session_failed(std::span<const std::byte> b);
std::optional<RelayNetworkStatusEvent> decode_relay_network_status(std::span<const std::byte> b);

}  // namespace nrsc::platform::steam
