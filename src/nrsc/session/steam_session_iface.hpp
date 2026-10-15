#pragma once
// Steam calls the session layer needs that server/steam_iface.hpp does not declare. Separate abstract interfaces so that
// server/* stays untouched; a platform adapter implements them on top of the real Steamworks SDK, tests mock them.
// Grades: the original's callers of these are in protected code (U), the signatures are the public Steamworks ones.
#include <cstdint>

#include "nrsc/server/steam_iface.hpp"

namespace nrsc::session {

// ISteamMatchmaking, the lobby control half (W: the original's call sites are not recovered).
class ISteamLobbyControl {
public:
    virtual ~ISteamLobbyControl() = default;
    // ISteamMatchmaking::CreateLobby(eLobbyType, cMaxMembers); the result arrives as LobbyCreated_t (0x201).
    virtual void CreateLobby(int lobby_type, int max_members) = 0;
    // ISteamMatchmaking::JoinLobby; the result arrives as LobbyEnter_t (0x1f8).
    virtual void JoinLobby(server::SteamId lobby) = 0;
    virtual void LeaveLobby(server::SteamId lobby) = 0;
    // ISteamMatchmaking::RequestLobbyData; the result arrives as LobbyDataUpdate_t (0x1f9). False if not sent.
    virtual bool RequestLobbyData(server::SteamId lobby) = 0;
};

// ISteamNetworkingMessages::GetSessionConnectionInfo (vtbl+0x28, C call shape: identity {type 0x10, id}, NULL, &quality).
class ISteamSessionInfo {
public:
    virtual ~ISteamSessionInfo() = default;
    // Fills the ping (SteamNetConnectionRealTimeStatus_t::m_nPing). Return value is ignored by the original (C, t_T2).
    virtual bool GetSessionConnectionInfo(server::SteamId remote, std::int32_t& ping_ms) = 0;
};

inline constexpr int kLobbyTypeFriendsOnly = 1;  // k_ELobbyTypeFriendsOnly (C: the CreateLobby hook forwards Public as 1)
inline constexpr int kLobbyTypePublic = 2;       // k_ELobbyTypePublic: the game's matchmaking lobby (docs/devirt.md 2.11)
inline constexpr int kLobbyMaxMembers = 6;       // peer pool 5 + local player (S from the pool size; W)

}  // namespace nrsc::session
