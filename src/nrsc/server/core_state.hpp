#pragma once

#include <cstdint>

#include "nrsc/server/steam_iface.hpp"

namespace nrsc::server {

// The parts of the server_manager core object that the readable code reads and writes.
struct CoreState {
    SteamId own_id = 0;                          // core+8 (compared against lobby members and leaderboard entries)
    std::uint64_t state_flags = 0;               // core+0: bit 0 set on lobby enter, bit 2 on lobby created (C)
    SteamId lobby_id = 0;                        // core+0x120
    SteamId owner_id = 0;                        // core+0x128 (lobby owner)
    bool lobby_create_pending = false;           // core+0x158
    bool leaderboard_add_pending = false;        // core+0x159
    bool leaderboard_refresh_pending = false;    // core+0x15a
    int channel = 0;                             // ctx+4: nRemoteChannel / receive channel (S)
    std::uint32_t max_len = 0;                   // ctx+0xc: max payload length for flag==0 packets (S)
    std::uint32_t relay_status = 0;              // core+0x70: SteamRelayNetworkStatus_t::m_eAvail (0x180064430, C)
    SteamId join_request_lobby = 0;              // core+0x130: GameLobbyJoinRequested_t lobby (0x1800643c0, C)
    SteamId join_request_friend = 0;             // core+0x138: GameLobbyJoinRequested_t friend (0x1800643c0, C)
    bool closed = false;                         // core+0xc4 == 0x7fffffff: core closed/poisoned (0x1800644f3, C)
};

inline constexpr std::uint64_t kStateLobbyEntered = 0x1;
inline constexpr std::uint64_t kStateLobbyCreated = 0x4;
inline constexpr std::uint64_t kStateJoinRequested = 0x10;  // set by 0x1800643c0 (`or qword [core], 0x10`, C)

}  // namespace nrsc::server
