#include "nrsc/server/callbacks.hpp"

#include <system_error>
#include <utility>

namespace nrsc::server {

void register_startup_handlers(MessageBus& bus, MessageHandler sub_header, MessageHandler broadcast,
                               MessageHandler peer_channel) {
    bus.register_handler(kMsgSubHeader, std::move(sub_header));
    bus.register_handler(kMsgBroadcast, std::move(broadcast));
    bus.register_handler(kMsgIgnored, [](Peer&, std::span<const std::byte>, std::uint32_t) {});
    bus.register_handler(kMsgData, std::move(peer_channel));
}

void on_game_lobby_join_requested(CoreState& core, SteamId lobby, SteamId friend_id) {
    core.join_request_lobby = lobby;
    core.join_request_friend = friend_id;
    core.state_flags |= kStateJoinRequested;
}

void on_relay_network_status(CoreState& core, std::uint32_t avail) { core.relay_status = avail; }

void on_session_request(CoreState& core, ISteamNetworkingMessages& net, SteamId remote) {
    if (core.closed)
        throw std::system_error(std::make_error_code(std::errc::resource_unavailable_try_again));
    net.AcceptSessionWithUser(remote);
}

}  // namespace nrsc::server
