#pragma once

#include <cstddef>
#include <span>

#include "nrsc/platform/steam/steam_decode.hpp"

// Dispatcher interface for the 8 Steam callbacks nrsc.dll registers (server::kRegisteredCallbacks). The Windows
// adapter decodes the raw callback struct (steam_decode) and calls exactly one method per callback. Methods run on the
// thread that calls SteamAPI_RunCallbacks (the game's Steam pump), like the original callback objects.
namespace nrsc::platform::steam {

// Steamworks callback ids (identical to server::SteamCallback, repeated so this header is server-independent).
enum CallbackId : int {
    kCbLobbyCreated = 0x201,
    kCbLobbyEnter = 0x1f8,
    kCbLobbyChatUpdate = 0x1fa,
    kCbSessionRequest = 0x4e3,
    kCbSessionFailed = 0x4e4,
    kCbRelayNetworkStatus = 0x501,
    kCbGameLobbyJoinRequested = 0x14d,
    kCbLobbyDataUpdate = 0x1f9,
};

struct ISteamCallbackSink {
    virtual ~ISteamCallbackSink() = default;
    virtual void on_lobby_created(const LobbyCreatedEvent&) = 0;
    virtual void on_lobby_enter(const LobbyEnterEvent&) = 0;
    virtual void on_lobby_chat_update(const LobbyChatUpdateEvent&) = 0;
    virtual void on_lobby_data_update(const LobbyDataUpdateEvent&) = 0;
    virtual void on_game_lobby_join_requested(const GameLobbyJoinRequestedEvent&) = 0;
    virtual void on_session_request(const SessionRequestEvent&) = 0;
    virtual void on_session_failed(const SessionFailedEvent&) = 0;
    virtual void on_relay_network_status(const RelayNetworkStatusEvent&) = 0;
};

// Decodes `param` for callback `id` and calls the matching sink method. Returns false for an unknown id or a buffer
// shorter than the struct (nothing is dispatched then).
bool dispatch_steam_callback(int id, std::span<const std::byte> param, ISteamCallbackSink& sink);

}  // namespace nrsc::platform::steam
