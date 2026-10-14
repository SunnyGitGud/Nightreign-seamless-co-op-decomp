#include "nrsc/server/message_bus.hpp"

#include <algorithm>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/server/wire.hpp"

namespace nrsc::server {

int MessageBus::poll() {
    auto messages = net_.ReceiveMessagesOnChannel(core_.channel, kMaxPollMessages);
    if (messages.size() > std::size_t(kMaxPollMessages)) messages.resize(kMaxPollMessages);
    int dispatched = 0;
    for (const ReceivedMessage& msg : messages) {
        const SteamId sender = msg.identity_type == kIdentityTypeSteamId ? msg.steam_id : 0;
        Peer* peer = peers_.find_active(sender);
        if (!peer) continue;
        if (!wire::poll_accepts(msg.data, core_.max_len)) continue;
        const auto it = handlers_.find(std::to_integer<std::uint8_t>(msg.data[0]));
        if (it == handlers_.end()) continue;
        it->second(*peer, msg.data, std::uint32_t(msg.data.size() - wire::kHeaderSize));
        ++dispatched;
    }
    return dispatched;
}

bool MessageBus::send(SteamId target, std::span<const std::byte> payload, std::uint8_t channel) {
    if (!peers_.find_active(target)) return false;
    const bool listed = std::find(unreliable_channels_.begin(), unreliable_channels_.end(), std::int32_t(channel)) !=
                        unreliable_channels_.end();
    const int flags = listed ? kSendUnreliableNoDelay : kSendReliableNoNagle;
    const auto packet = wire::encode({kMsgData, channel, 0, std::uint32_t(payload.size())}, payload);
    return net_.SendMessageToUser(target, packet, flags, core_.channel) == kResultOk;
}

void MessageBus::send_control(const Peer& peer) {
    const std::byte bit = std::byte((core_.state_flags >> 3) & 1);
    const auto packet = wire::encode({kMsgData, kControlChannel, 0, 1}, std::span<const std::byte>(&bit, 1));
    net_.SendMessageToUser(peer.steam_id, packet, kSendReliableNoNagle, core_.channel);
}

void dispatch_game_component(const GameComponentMap& handlers, int key, void* arg) {
    const auto it = handlers.find(key);
    if (it == handlers.end())
        throw FatalError(kGameComponentFatalWhat, kServerManagerSource, kGameComponentFatalLine);
    it->second(key, arg);
}

}  // namespace nrsc::server
