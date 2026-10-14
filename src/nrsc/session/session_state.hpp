#pragma once
// SessionState: the part of nrsc's master state object `M` (the original's `*ctx[0xe0]`) that the lobby/join/frame code
// works on (docs/phase3_vm_networking.md 1.2, 3). It wraps server::CoreState, the struct the readable server code
// (Lobby, MessageBus, on_session_request ...) already uses, so that both views are the same memory:
//   M+0x00 flags      = core.state_flags       M+0x120/0x128 lobby/owner = core.lobby_id/owner_id
//   M+0x130 join lobby = core.join_request_lobby   M+0x138 join friend = core.join_request_friend
//   M+0x140 join error = join_error (here; CoreState has no such field and server/* must not change)
// Thread safety: none by itself; session::Session serialises every access with its mutex.
#include <cstdint>

#include "nrsc/server/core_state.hpp"
#include "nrsc/server/peer.hpp"

namespace nrsc::session {

using SteamId = server::SteamId;

// Flag bits of M[0] (C: bit0/bit2 readable handlers, bit4 0x1800643c0/0x180064120; S: bit3 = the bit that the control
// message carries (0x18003f2c0); W: bit5 = party, the "0x21" mode word is bit0 | bit5).
enum StateFlag : std::uint64_t {
    kInLobby = 0x1,
    kLobbyCreated = 0x4,
    kBit3 = 0x8,
    kJoinPending = 0x10,
    kParty = 0x20,
};
static_assert(kInLobby == server::kStateLobbyEntered && kLobbyCreated == server::kStateLobbyCreated &&
              kJoinPending == server::kStateJoinRequested);

// Failure words written to M+0x140 by 0x180064120 (C, written as 8 bytes).
inline constexpr std::uint64_t kJoinErrorNoSeamless = 0xFFFFFFFFFFFFE0C1ull;
inline constexpr std::uint64_t kJoinErrorWrongVersion = 0xFFFFFFFFFFFFE0C2ull;

// Peer flag bits added by 0x180061140 on top of server::kPeerActive (0x01) / kPeerHost (0x20) (C, phase3 2.2).
inline constexpr std::uint32_t kPeerHandshaken = 0x10;   // first control message received
inline constexpr std::uint32_t kPeerRemoteBit3 = 0x40;   // remote reported state bit3 in that message
inline constexpr std::uint32_t kPeerStateMask = 0x11;    // tested by the frame task: (flags & 0x11) == 0x01 / 0x11

struct SessionState {
    server::CoreState core;
    std::uint64_t join_error = 0;  // M+0x140

    std::uint64_t& flags() { return core.state_flags; }
    std::uint64_t flags() const { return core.state_flags; }
    bool has(StateFlag f) const { return (core.state_flags & f) != 0; }
    void set(StateFlag f) { core.state_flags |= f; }
    void clear(StateFlag f) { core.state_flags &= ~std::uint64_t{f}; }

    SteamId& join_lobby() { return core.join_request_lobby; }
    SteamId& join_friend() { return core.join_request_friend; }
    SteamId join_lobby() const { return core.join_request_lobby; }
    SteamId join_friend() const { return core.join_request_friend; }
    void clear_join_target() { core.join_request_lobby = 0; core.join_request_friend = 0; }
};

}  // namespace nrsc::session
