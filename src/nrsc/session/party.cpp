#include "nrsc/session/party.hpp"

namespace nrsc::session {

void PartyTracker::update() {
    bool any = false;
    if (state_.has(kInLobby))
        for (std::size_t i = 0; i < peers_.size() && !any; ++i)
            any = (peers_.slot(i).flags & kPeerStateMask) == kPeerStateMask;
    if (any) state_.set(kParty); else state_.clear(kParty);
    if (any != active_) {
        active_ = any;
        if (setter_) setter_(any);
    }
}

}  // namespace nrsc::session
