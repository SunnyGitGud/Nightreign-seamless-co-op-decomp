#pragma once

#include <cstdint>

#include "nrsc/server/core_state.hpp"
#include "nrsc/server/message_bus.hpp"
#include "nrsc/server/steam_iface.hpp"

// Startup wiring of server_manager, recovered from the real-image emulation of the startup thread 0x180040630
// (docs/devirt.md 2.9, devirt/out/v4/summary.json "handlers" and "callbacks"). The registration itself happens in
// protected code; the tables and the readable callback bodies are reconstructed here.
namespace nrsc::server {

// The 8 SteamAPI_RegisterCallback ids, in registration order (C). The struct names are the public Steamworks names
// for these ids (S). The run slot is the callback object's vtable slot 1.
enum class SteamCallback : int {
    LobbyCreated = 0x201,               // run 0x180064a50 (on_lobby_created, lobby.hpp)
    LobbyEnter = 0x1f8,                 // run 0x1800647b0 (on_lobby_enter, lobby.hpp)
    LobbyChatUpdate = 0x1fa,            // run 0x1800645b0 (on_chat_update, lobby.hpp)
    SessionRequest = 0x4e3,             // run 0x1800644c0 (on_session_request below)
    SessionFailed = 0x4e4,              // run 0x180001080 (no-op, C)
    RelayNetworkStatus = 0x501,         // run 0x180064430 (on_relay_network_status below)
    GameLobbyJoinRequested = 0x14d,     // run 0x1800643c0 (on_game_lobby_join_requested below)
    LobbyDataUpdate = 0x1f9,            // run 0x180064120 (protected; body U beyond devirt.md 2.4)
};

inline constexpr SteamCallback kRegisteredCallbacks[] = {
    SteamCallback::LobbyCreated,   SteamCallback::LobbyEnter,         SteamCallback::LobbyChatUpdate,
    SteamCallback::SessionRequest, SteamCallback::SessionFailed,      SteamCallback::RelayNetworkStatus,
    SteamCallback::GameLobbyJoinRequested, SteamCallback::LobbyDataUpdate,
};

// Message ids registered at startup through the map emplace 0x180063570 (C): 0 -> 0x18003f4a0 (receiver with the
// 8-byte sub-header), 1 -> 0x180040130 (broadcast receiver), 2 -> 0x180004c80 (`mov al,1; ret`: accepted and
// ignored), 3 -> 0x180040240 (peer-channel receiver). The receivers' queue logic is not modelled, so the caller
// supplies them; id 2 is registered as a no-op here.
inline constexpr std::uint8_t kMsgSubHeader = 0x00;
inline constexpr std::uint8_t kMsgBroadcast = 0x01;
inline constexpr std::uint8_t kMsgIgnored = 0x02;
static_assert(kMsgData == 0x03);

void register_startup_handlers(MessageBus& bus, MessageHandler sub_header, MessageHandler broadcast,
                               MessageHandler peer_channel);

// 0x1800643c0 (C): copies {lobby, friend} into core+0x130/+0x138 and sets core state bit 0x10.
void on_game_lobby_join_requested(CoreState& core, SteamId lobby, SteamId friend_id);

// 0x180064430 (C): stores the first dword of SteamRelayNetworkStatus_t at core+0x70.
void on_relay_network_status(CoreState& core, std::uint32_t avail);

// 0x1800644c0 (C): under the core lock (not modelled), accepts the session with AcceptSessionWithUser (vtbl+0x10).
// If the core is closed (core+0xc4 == 0x7fffffff) the original marks it 0x7ffffffe and calls FUN_18008ee9c(6), the
// MSVC std::system_error helper (S: code 6 = resource_unavailable_try_again); this throws the same.
void on_session_request(CoreState& core, ISteamNetworkingMessages& net, SteamId remote);

// 0x180001080 (C): SessionFailed is registered but ignored.
inline void on_session_failed() {}

}  // namespace nrsc::server
