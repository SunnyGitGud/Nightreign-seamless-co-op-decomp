#pragma once
// PartyTracker (W): drives the party session word (hooks::set_party_session_active; 0 alone, 0x21 = bits 0|5 in a party).
// Condition (W, phase3 3): in a lobby AND at least one active peer has completed the handshake (flag 0x10). The setter
// is only called when the value changes. kParty (0x20) of SessionState::flags mirrors the result.
#include <functional>

#include "nrsc/hooks/session_word.hpp"
#include "nrsc/server/peer.hpp"
#include "nrsc/session/session_state.hpp"

namespace nrsc::session {

class PartyTracker {
public:
    using Setter = std::function<void(bool)>;
    PartyTracker(SessionState& state, const server::PeerList& peers, Setter setter = hooks::set_party_session_active)
        : state_(state), peers_(peers), setter_(std::move(setter)) {}

    void update();
    bool active() const { return active_; }

private:
    SessionState& state_;
    const server::PeerList& peers_;
    Setter setter_;
    bool active_ = false;
};

}  // namespace nrsc::session
