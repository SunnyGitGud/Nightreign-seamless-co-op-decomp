#include "nrsc/platform/steam/steam_callbacks.hpp"

namespace nrsc::platform::steam {

bool dispatch_steam_callback(int id, std::span<const std::byte> p, ISteamCallbackSink& sink) {
    switch (id) {
        case kCbLobbyCreated:
            if (auto e = decode_lobby_created(p)) return sink.on_lobby_created(*e), true;
            return false;
        case kCbLobbyEnter:
            if (auto e = decode_lobby_enter(p)) return sink.on_lobby_enter(*e), true;
            return false;
        case kCbLobbyChatUpdate:
            if (auto e = decode_lobby_chat_update(p)) return sink.on_lobby_chat_update(*e), true;
            return false;
        case kCbLobbyDataUpdate:
            if (auto e = decode_lobby_data_update(p)) return sink.on_lobby_data_update(*e), true;
            return false;
        case kCbGameLobbyJoinRequested:
            if (auto e = decode_game_lobby_join_requested(p)) return sink.on_game_lobby_join_requested(*e), true;
            return false;
        case kCbSessionRequest:
            if (auto e = decode_session_request(p)) return sink.on_session_request(*e), true;
            return false;
        case kCbSessionFailed:
            if (auto e = decode_session_failed(p)) return sink.on_session_failed(*e), true;
            return false;
        case kCbRelayNetworkStatus:
            if (auto e = decode_relay_network_status(p)) return sink.on_relay_network_status(*e), true;
            return false;
        default:
            return false;
    }
}

}  // namespace nrsc::platform::steam
