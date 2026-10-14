#pragma once

#include <functional>
#include <string_view>

#include "nrsc/server/core_state.hpp"
#include "nrsc/server/peer.hpp"
#include "nrsc/server/steam_iface.hpp"

// Steam lobby callbacks of server_manager (docs/server_manager.md 2.1).
namespace nrsc::server {

inline constexpr std::string_view kLobbyTypeKey = "nrsc_lobby_type";                           // C (0x180064844)
inline constexpr std::string_view kMasterLobby = "nrsc_ynx3_seamless_master_lobby";            // C, 31 chars + NUL = 0x20
inline constexpr std::string_view kLobbyVersionKey = "nrsc_lobby_version";                     // C (0x180064c1b)
inline constexpr std::string_view kModVersion = "1.1.3";                                       // C (0x18014d82a)
inline constexpr std::uint32_t kLeaveMask = 0x10114;  // EChatMemberStateChange 2|4|8|16 = Left|Disconnected|Kicked|Banned
inline constexpr std::uint32_t kChatEntered = 1;
inline constexpr std::uint32_t kChatLeft = 2, kChatDisconnected = 4, kChatKicked = 8, kChatBanned = 16;

struct LobbyCreated {      // LobbyCreated_t. The result (+0) is never read by the callback (C); only the id at +8.
    std::int32_t result = 0;
    SteamId lobby_id = 0;
};
struct LobbyEnter {        // LobbyEnter_t: lobby id at +0 (C)
    SteamId lobby_id = 0;
};
struct LobbyChatUpdate {   // LobbyChatUpdate_t: lobby +0, user changed +8, state change +0x18 (C)
    SteamId lobby_id = 0;
    SteamId user_changed = 0;
    std::uint32_t state_change = 0;
};

class Lobby {
public:
    // on_leave is the forwarded "peer left" event (cb vtbl+0x18 with an identity {0x10, steam id}, 0x180064649).
    using LeaveHandler = std::function<void(SteamId)>;

    Lobby(CoreState& core, PeerList& peers, ISteamMatchmaking& mm, LeaveHandler on_leave = {})
        : core_(core), peers_(peers), mm_(mm), on_leave_(std::move(on_leave)) {}

    // 0x180064a50 (C). Does nothing unless create-pending was set; the flag is cleared first (atomic exchange) whatever
    // happens next. Sets both lobby data keys (stopping at the first failure); on success stores lobby id and
    // owner (= own id, core+8 read at 0x180064c35: S) and sets state bit 2. Returns true when the lobby was adopted.
    bool OnLobbyCreated(const LobbyCreated& ev);

    // 0x1800647b0 (C). GetLobbyData("nrsc_lobby_type") must equal the master-lobby literal (memcmp of 0x20 bytes
    // incl. the NUL, which for a C string is exact equality); mismatch is ignored silently. On match: for each member
    // other than ourselves with no active peer yet, claims a free peer slot, activates it and sets bit 5 iff the member
    // is the lobby owner; then stores lobby id, owner id and sets state bit 0. nrsc_lobby_version is NOT compared
    // here (U whether protected code does). Returns true when the lobby was adopted.
    bool OnLobbyEnter(const LobbyEnter& ev);

    // 0x1800645b0 (C). Ignored when the changed user is ourselves or the lobby is not ours. Leave states (mask
    // 0x10114) for an active peer: reset the peer and forward the leave event. Entered (1): claim a free slot for the
    // user unless an active peer exists (the host bit is not set on this path).
    void OnChatUpdate(const LobbyChatUpdate& ev);

private:
    CoreState& core_;
    PeerList& peers_;
    ISteamMatchmaking& mm_;
    LeaveHandler on_leave_;
};

}  // namespace nrsc::server
