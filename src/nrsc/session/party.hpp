#pragma once
// PartyTracker: drives the session word (hooks::set_session_word). It mirrors M[0] of the original (C, live session records
// 2026-10-16): bit 0 while in a lobby, bit 2 when the lobby is our own (host: 0x05, never bit 5), bit 5 on a guest once
// at least one active peer has completed the handshake (flag 0x10; guest: 0x01 -> 0x21, the original sets it 1.4-2.4 s
// after lobby entry, W for the exact trigger). The setter is only called when the value changes. kParty (0x20) of
// SessionState::flags mirrors bit 5; active() is "a handshaken peer exists" in either role.
#include <cstdint>
#include <functional>

#include "nrsc/hooks/session_word.hpp"
#include "nrsc/server/peer.hpp"
#include "nrsc/session/session_state.hpp"

namespace nrsc::session {

class PartyTracker {
public:
    using Setter = std::function<void(std::uint32_t)>;
    PartyTracker(SessionState& state, const server::PeerList& peers, Setter setter = hooks::set_session_word)
        : state_(state), peers_(peers), setter_(std::move(setter)) {}

    void update();
    bool active() const { return active_; }
    std::uint32_t word() const { return word_; }

private:
    SessionState& state_;
    const server::PeerList& peers_;
    Setter setter_;
    bool active_ = false;
    std::uint32_t word_ = 0;
};

}  // namespace nrsc::session
