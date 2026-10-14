#include "nrsc/server/lobby.hpp"

namespace nrsc::server {

bool Lobby::OnLobbyCreated(const LobbyCreated& ev) {
    const bool pending = core_.lobby_create_pending;
    core_.lobby_create_pending = false;
    if (!pending) return false;
    if (!mm_.SetLobbyData(ev.lobby_id, std::string(kLobbyTypeKey), std::string(kMasterLobby))) return false;
    if (!mm_.SetLobbyData(ev.lobby_id, std::string(kLobbyVersionKey), std::string(kModVersion))) return false;
    core_.lobby_id = ev.lobby_id;
    core_.owner_id = core_.own_id;
    core_.state_flags |= kStateLobbyCreated;
    return true;
}

bool Lobby::OnLobbyEnter(const LobbyEnter& ev) {
    if (mm_.GetLobbyData(ev.lobby_id, std::string(kLobbyTypeKey)) != kMasterLobby) return false;
    const std::int32_t count = mm_.GetNumLobbyMembers(ev.lobby_id);
    const SteamId owner = mm_.GetLobbyOwner(ev.lobby_id);
    for (std::int32_t i = 0; i < count; ++i) {
        const SteamId member = mm_.GetLobbyMemberByIndex(ev.lobby_id, i);
        if (member == core_.own_id) continue;
        if (peers_.find_active(member)) continue;
        Peer* slot = peers_.find_free();
        if (!slot) continue;
        PeerList::activate(*slot, member);
        // flags = (flags & ~0x20) | ((member == owner) << 5), 0x1800649f4..0x1800649ff
        slot->flags = (slot->flags & ~kPeerHost) | (member == owner ? kPeerHost : 0);
    }
    core_.owner_id = owner;
    core_.lobby_id = ev.lobby_id;
    core_.state_flags |= kStateLobbyEntered;
    return true;
}

void Lobby::OnChatUpdate(const LobbyChatUpdate& ev) {
    if (ev.user_changed == core_.own_id) return;
    if (ev.lobby_id != core_.lobby_id) return;
    if (ev.state_change > 0x10) return;
    if ((kLeaveMask >> ev.state_change) & 1u) {
        Peer* peer = peers_.find_active(ev.user_changed);
        if (!peer) return;
        PeerList::reset(*peer);
        if (on_leave_) on_leave_(ev.user_changed);
        return;
    }
    if (ev.state_change == kChatEntered) {
        if (peers_.find_active(ev.user_changed)) return;
        if (Peer* slot = peers_.find_free()) PeerList::activate(*slot, ev.user_changed);
    }
}

}  // namespace nrsc::server
