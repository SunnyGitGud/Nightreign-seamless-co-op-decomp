#include "nrsc/platform/steam/steam_decode.hpp"

#include <cstring>

namespace nrsc::platform::steam {
namespace {

template <typename T>
T rd(std::span<const std::byte> b, std::size_t off) {
    T v{};
    std::memcpy(&v, b.data() + off, sizeof(T));
    return v;
}

}  // namespace

SteamNetworkingIdentity make_steamid_identity(SteamId id) {
    SteamNetworkingIdentity i;
    i.type = static_cast<std::int32_t>(kIdentityTypeSteamId);
    i.size = 8;
    std::memcpy(i.data, &id, sizeof id);
    return i;
}

std::optional<SteamId> identity_steam_id(const SteamNetworkingIdentity& identity) {
    if (identity.type != static_cast<std::int32_t>(kIdentityTypeSteamId)) return std::nullopt;
    SteamId id = 0;
    std::memcpy(&id, identity.data, sizeof id);
    return id;
}

std::optional<MessageHeader> decode_message_header(std::span<const std::byte> m) {
    if (m.size() < kMsgOffChannel + 4) return std::nullopt;
    MessageHeader h;
    h.data = rd<const void*>(m, kMsgOffData);
    h.size = rd<std::int32_t>(m, kMsgOffSize);
    h.identity_type = rd<std::uint32_t>(m, kMsgOffIdentity);
    if (h.identity_type == kIdentityTypeSteamId) h.steam_id = rd<SteamId>(m, kMsgOffIdentity + 8);
    h.channel = rd<std::int32_t>(m, kMsgOffChannel);
    return h;
}

std::optional<LobbyCreatedEvent> decode_lobby_created(std::span<const std::byte> b) {
    if (b.size() < kLobbyCreatedSize) return std::nullopt;
    return LobbyCreatedEvent{rd<std::int32_t>(b, 0), rd<SteamId>(b, 8)};
}

std::optional<LobbyEnterEvent> decode_lobby_enter(std::span<const std::byte> b) {
    if (b.size() < kLobbyEnterSize) return std::nullopt;
    return LobbyEnterEvent{rd<SteamId>(b, 0), rd<std::uint32_t>(b, 8), rd<std::uint8_t>(b, 12) != 0,
                           rd<std::uint32_t>(b, 16)};
}

std::optional<LobbyChatUpdateEvent> decode_lobby_chat_update(std::span<const std::byte> b) {
    if (b.size() < kLobbyChatUpdateSize) return std::nullopt;
    return LobbyChatUpdateEvent{rd<SteamId>(b, 0), rd<SteamId>(b, 8), rd<SteamId>(b, 16), rd<std::uint32_t>(b, 24)};
}

std::optional<LobbyDataUpdateEvent> decode_lobby_data_update(std::span<const std::byte> b) {
    if (b.size() < kLobbyDataUpdateSize) return std::nullopt;
    return LobbyDataUpdateEvent{rd<SteamId>(b, 0), rd<SteamId>(b, 8), rd<std::uint8_t>(b, 16) != 0};
}

std::optional<GameLobbyJoinRequestedEvent> decode_game_lobby_join_requested(std::span<const std::byte> b) {
    if (b.size() < kGameLobbyJoinRequestedSize) return std::nullopt;
    return GameLobbyJoinRequestedEvent{rd<SteamId>(b, 0), rd<SteamId>(b, 8)};
}

std::optional<SessionRequestEvent> decode_session_request(std::span<const std::byte> b) {
    if (b.size() < kSessionRequestSize) return std::nullopt;
    SessionRequestEvent e;
    std::memcpy(&e.identity, b.data(), kIdentitySize);
    if (auto id = identity_steam_id(e.identity)) {
        e.steam_id = *id;
        e.is_steam_id = true;
    }
    return e;
}

std::optional<SessionFailedEvent> decode_session_failed(std::span<const std::byte> b) {
    if (b.size() < kSessionFailedSize) return std::nullopt;
    SessionFailedEvent e;
    SteamNetworkingIdentity i;
    std::memcpy(&i, b.data(), kIdentitySize);
    if (auto id = identity_steam_id(i)) {
        e.steam_id = *id;
        e.is_steam_id = true;
    }
    e.state = rd<std::int32_t>(b, 176);
    e.end_reason = rd<std::int32_t>(b, 180);
    return e;
}

std::optional<RelayNetworkStatusEvent> decode_relay_network_status(std::span<const std::byte> b) {
    if (b.size() < kRelayStatusMinSize) return std::nullopt;
    return RelayNetworkStatusEvent{rd<std::int32_t>(b, 0)};
}

}  // namespace nrsc::platform::steam
