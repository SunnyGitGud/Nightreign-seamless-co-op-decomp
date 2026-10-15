#include "nrsc/session/party.hpp"

namespace nrsc::session {

void PartyTracker::update() {
    bool any = false;
    std::uint32_t word = 0;
    if (state_.has(kInLobby)) {
        for (std::size_t i = 0; i < peers_.size() && !any; ++i)
            any = (peers_.slot(i).flags & kPeerStateMask) == kPeerStateMask;
        word = static_cast<std::uint32_t>(kInLobby);
        if (state_.has(kLobbyCreated)) word |= static_cast<std::uint32_t>(kLobbyCreated);  // host: 0x05, no bit 5
        else if (any) word |= static_cast<std::uint32_t>(kParty);                          // guest: 0x21
    }
    if (word & kParty) state_.set(kParty); else state_.clear(kParty);
    active_ = any;
    if (word != word_) {
        word_ = word;
        if (setter_) setter_(word);
    }
}

}  // namespace nrsc::session
