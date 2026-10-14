#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "nrsc/server/steam_iface.hpp"

namespace nrsc::server {

inline constexpr std::uint32_t kPeerActive = 0x01;  // peer+0x128 bit 0 (C: tested with `test byte [peer+0x128], 1`)
inline constexpr std::uint32_t kPeerHost = 0x20;    // bit 5 = lobby owner (C: 0x180064a0c uses `and ~0x20`, `shl 5`)
// 0x180061140 rewrites a peer whose flag byte is exactly 1 to 0x11 (bit 4 added). What bit 4 means is U.
inline constexpr std::uint32_t kPeerMarked = 0x11;

// Peer (ctx+0x28 vector element). Only the fields the readable code touches are kept: steam id (peer+8), flags
// (peer+0x128), field3c (peer+0x3c, set to 1 by 0x180061140). The per-channel receive queues (peer+0x398 +
// ch*0x268, peer+0x96770 + ch*0x268), the persona name and the string fields are not modelled.
struct Peer {
    SteamId steam_id = 0;
    std::uint32_t flags = 0;
    std::uint32_t field3c = 0;

    bool active() const { return (flags & kPeerActive) != 0; }
    bool host() const { return (flags & kPeerHost) != 0; }
};

// The peer list is a vector of preallocated slots; the lobby callbacks claim the first slot whose active bit is clear
// (0x1800649bd..0x1800649ce, 0x1800646f9..0x180064701). The real slot count is U (the pool is created in protected
// code), so the capacity is a constructor parameter.
// Peer pool size seen in the real-image run: the queue ctor 0x180064c60 runs 5 x (1 + 999 + 9) times, one 0x97d18-byte
// block per peer (docs/devirt.md 2.9, C). PeerList keeps its own default; construct it with kPeerPoolSize to match.
inline constexpr std::size_t kPeerPoolSize = 5;

class PeerList {
public:
    explicit PeerList(std::size_t capacity = 8) : slots_(capacity) {}

    std::size_t size() const { return slots_.size(); }
    Peer& slot(std::size_t i) { return slots_.at(i); }
    const Peer& slot(std::size_t i) const { return slots_.at(i); }
    std::vector<Peer>& slots() { return slots_; }

    // First active peer with this id (loop at 0x18003f08d / 0x180064989 / 0x180063240). C.
    Peer* find_active(SteamId id);
    // First slot whose active bit is clear (0x1800649bd). C.
    Peer* find_free();
    // 0x18003d920 (peer init: also fetches the persona name via the core Steam interface, vtbl+0x38). W for the exact
    // fields: this models the visible effect, id set and slot active. The host bit is cleared.
    static void activate(Peer& peer, SteamId id);
    // 0x18003ee30 (peer reset: zeroes the id and string fields). W for flags: modelled as a full clear.
    static void reset(Peer& peer);

private:
    std::vector<Peer> slots_;
};

}  // namespace nrsc::server
