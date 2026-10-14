#include "nrsc/server/peer.hpp"

namespace nrsc::server {

Peer* PeerList::find_active(SteamId id) {
    for (Peer& p : slots_)
        if (p.active() && p.steam_id == id) return &p;
    return nullptr;
}

Peer* PeerList::find_free() {
    for (Peer& p : slots_)
        if (!p.active()) return &p;
    return nullptr;
}

void PeerList::activate(Peer& peer, SteamId id) {
    peer.steam_id = id;
    peer.flags = kPeerActive;
}

void PeerList::reset(Peer& peer) { peer = Peer{}; }

}  // namespace nrsc::server
