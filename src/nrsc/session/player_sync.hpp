#pragma once
// PlayerSync: the party data sync and the F4 pause toggle of the per-frame task 0x180061140 (docs/devirt.md 2.10-2.12).
// All of it is C for the emulation (real startup state, planted game objects, taint and injection runs); the field names
// are S/W. Game memory and game functions are reached through IGamePlayers so the logic runs on fake memory in tests.
//
// Every tick, when the world object and the local PlayerGameData exist and CSSessionManager+0x10 is not >= 5 (a null
// manager does not block, C), for each party member whose SteamID is an active peer:
//   resolve   the member's remote PlayerGameData slot once: [[GameDataMan]+0x10] + member.slot * 0x8A0; its dwords +0x8 and
//             +0x98 are zeroed when it is first taken
//   receive   sub-channel 3 (28 bytes) -> decode_player_state into the slot; sub-channel 4 (504 bytes) -> game
//             deserializer 0x14020dcb0(slot, buf)
//   send      sub-channel 3 = encode_player_state(local PlayerGameData); sub-channel 4 = game serializer
//             0x14020bde0(local, buf) (always 504 bytes, the serializer returns false and the original sends anyway)
// F4: GetAsyncKeyState(VK_F4) == pressed-since-last-call (0x8001) in the original (the rebuild's Windows adapter uses an
// up->down edge of 0x8000 instead, which also works under Proton) toggles the pause byte (the #14 "force true" flag,
// NetState state +0x195) and shows MODMESSAGE_YUINEXUSPAUSED / RESUMED (category 15000, ids 2 / 3) through the game's
// message display 0x14086a7a0.
#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "nrsc/hooks/net/net_group.hpp"
#include "nrsc/server/message_bus.hpp"
#include "nrsc/server/peer.hpp"

namespace nrsc::session {

inline constexpr std::uint8_t kPlayerStateChannel = 3;     // C: 28-byte state
inline constexpr std::uint8_t kPlayerDataChannel = 4;      // C: 504-byte serialized block
inline constexpr std::size_t kPlayerStateSize = 0x1c;      // C
inline constexpr std::size_t kPlayerDataSize = 0x1f8;      // C (0x14020bde0 writes up to +0x1f2)
inline constexpr std::size_t kRemotePlayerStride = 0x8a0;  // C: [GameDataMan+0x10] slot stride
inline constexpr std::uint32_t kSyncMaxSessionState = 5;   // C: data path only while CSSessionManager+0x10 < 5
inline constexpr std::int32_t kPauseMessageCategory = 15000, kPausedId = 2, kResumedId = 3;  // C (key table)

struct PartyMember {
    server::SteamId steam_id = 0;  // record +0x10
    std::int32_t slot = 0;         // record +0xe8
};

class IGamePlayers {
public:
    virtual ~IGamePlayers() = default;
    // Gate of both parts (C, ablation run): [0x143c0f0a8] (slot 0x188) and its +0x174e8 pointer are non-null.
    virtual bool world_ready() = 0;
    virtual std::optional<std::uint32_t> session_state() = 0;   // CSSessionManager+0x10; nullopt when the manager is null
    virtual std::vector<PartyMember> members() = 0;              // [0x143c21fc8]+0x30..+0x38, 0xF0-byte records
    virtual std::byte* local_player_data() = 0;                  // [[0x143c078d0]+8]
    virtual std::byte* remote_player_table() = 0;                // [[0x143c078d0]+0x10]
    virtual void serialize(std::byte* player_data, std::byte* out) = 0;          // 0x14020bde0 (slot 0x60)
    virtual void deserialize(std::byte* player_data, const std::byte* in) = 0;   // 0x14020dcb0 (slot 0x68)
    virtual bool f4_pressed() = 0;                                // one new F4 press since the last tick
    // 0x14086a7a0 (slot 0xb8) with a std::wstring. It returns a non-owning {pointer, id} pair (no add-ref, devirt 2.13);
    // it shows nothing (returns {0, 0}) while its display singleton +0x14c is set.
    virtual void show_message(const std::u16string& text) = 0;
};

// The 28-byte sub-channel 3 payload from / into a PlayerGameData (C: taint and injection runs).
std::array<std::byte, kPlayerStateSize> encode_player_state(const std::byte* player_data);
void decode_player_state(std::span<const std::byte> payload, std::byte* player_data);

using TextSource = std::function<std::optional<std::u16string>(std::int32_t category, std::int32_t id)>;

class PlayerSync {
public:
    PlayerSync(server::PeerList& peers, server::MessageBus& bus, hooks::net::NetState& net)
        : peers_(peers), bus_(bus), net_(net) {}

    void set_game(IGamePlayers* game, TextSource text) {
        game_ = game;
        text_ = std::move(text);
    }
    void tick();  // call under the session mutex, while attached
    void forget_peer(server::SteamId id) {
        slots_.erase(id);
        rx_flags_.erase(id);
    }
    void reset() {
        slots_.clear();
        rx_flags_.clear();
    }
    bool paused() const { return paused_; }
    // Peer+0x38 (C, devirt 2.13): bit0 = do not send the state (sub-channel 3) to this peer, bit1 = do not send the data
    // block (sub-channel 4), bit2 = state received, bit3 = data block received. Only the task itself reads them; no writer of
    // bits 0/1 was found (S), so they stay clear unless set_peer_flags() is used.
    std::uint32_t rx_flags(server::SteamId id) const;
    void set_peer_flags(server::SteamId id, std::uint32_t flags) { rx_flags_[id] = flags; }

private:
    void pause_toggle();
    void sync_member(const PartyMember& m, std::byte* local);

    server::PeerList& peers_;
    server::MessageBus& bus_;
    hooks::net::NetState& net_;
    IGamePlayers* game_ = nullptr;
    TextSource text_;
    bool paused_ = false;
    std::map<server::SteamId, std::byte*> slots_;          // Peer+0x28 in the original
    std::map<server::SteamId, std::uint32_t> rx_flags_;    // Peer+0x38
};

}  // namespace nrsc::session
