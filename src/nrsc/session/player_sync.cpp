#include "nrsc/session/player_sync.hpp"

#include <cstring>

namespace nrsc::session {

namespace {

// (payload offset, PlayerGameData offset, size) of the plain fields, in payload order (C).
struct Field {
    std::size_t at, pgd, size;
};
constexpr Field kFields[] = {
    {0x00, 0xe8, 4}, {0x04, 0x68, 4}, {0x08, 0xc8, 4}, {0x0c, 0xcc, 4}, {0x10, 0x54, 4},
    {0x14, 0xe6, 1}, {0x15, 0xc2, 1}, {0x16, 0xe5, 1}, {0x17, 0x107, 1}, {0x1a, 0x187, 1}, {0x1b, 0x871, 1},
};
constexpr std::size_t kConstAt = 0x18;     // always 2 on send, ignored on receive (C)
constexpr std::byte kConstValue{2};
constexpr std::size_t kBitsAt = 0x19;      // bit0 +0x86d, bit1 +0xbe, bit2 +0x870 (C)
constexpr std::size_t kBit0 = 0x86d, kBit1 = 0xbe, kBit2 = 0x870;
constexpr std::size_t kMirrorOf54 = 0x58;  // the receiver also stores payload +0x10 here (C)
constexpr std::size_t kSlotClear1 = 0x8, kSlotClear2 = 0x98;  // zeroed when a slot is taken (C)

}  // namespace

std::array<std::byte, kPlayerStateSize> encode_player_state(const std::byte* pgd) {
    std::array<std::byte, kPlayerStateSize> out{};
    for (const Field& f : kFields) std::memcpy(out.data() + f.at, pgd + f.pgd, f.size);
    out[kConstAt] = kConstValue;
    // Bytes OR-ed as they are (the taint run gave 0xa7 | 0xa1 << 1 | 0xa8 << 2 = 0xe7); the game stores 0/1.
    const auto b0 = std::to_integer<unsigned>(pgd[kBit0]), b1 = std::to_integer<unsigned>(pgd[kBit1]),
               b2 = std::to_integer<unsigned>(pgd[kBit2]);
    out[kBitsAt] = std::byte(static_cast<std::uint8_t>(b0 | (b1 << 1) | (b2 << 2)));
    return out;
}

void decode_player_state(std::span<const std::byte> payload, std::byte* pgd) {
    if (payload.size() < kPlayerStateSize) return;
    for (const Field& f : kFields) std::memcpy(pgd + f.pgd, payload.data() + f.at, f.size);
    std::memcpy(pgd + kMirrorOf54, payload.data() + 0x10, 4);
    const auto bits = std::to_integer<unsigned>(payload[kBitsAt]);
    pgd[kBit0] = std::byte(bits & 1);
    pgd[kBit1] = std::byte((bits >> 1) & 1);
    pgd[kBit2] = std::byte((bits >> 2) & 1);
}

std::uint32_t PlayerSync::rx_flags(server::SteamId id) const {
    const auto it = rx_flags_.find(id);
    return it == rx_flags_.end() ? 0 : it->second;
}

void PlayerSync::tick() {
    if (!game_ || !game_->world_ready()) return;  // gates F4 and the sync (C, ablation)
    pause_toggle();
    std::byte* local = game_->local_player_data();
    if (!local) return;  // the sync also needs the local PlayerGameData (C); F4 does not
    const auto state = game_->session_state();
    if (state && *state >= kSyncMaxSessionState) return;  // a null manager does not block (C)
    for (const PartyMember& m : game_->members()) sync_member(m, local);
}

void PlayerSync::pause_toggle() {
    if (!game_->f4_pressed()) return;
    paused_ = !paused_;
    net_.state_blob[hooks::net::NetState::kStateForceFlag] = paused_ ? 1 : 0;
    if (text_) {
        if (auto text = text_(kPauseMessageCategory, paused_ ? kPausedId : kResumedId)) game_->show_message(*text);
    }
}

void PlayerSync::sync_member(const PartyMember& m, std::byte* local) {
    if (m.steam_id == 0 || !peers_.find_active(m.steam_id)) return;  // members that are not active peers: nothing (C)

    std::byte*& slot = slots_[m.steam_id];
    if (!slot) {
        if (std::byte* table = game_->remote_player_table()) {
            slot = table + static_cast<std::size_t>(m.slot) * kRemotePlayerStride;
            std::memset(slot + kSlotClear1, 0, 4);
            std::memset(slot + kSlotClear2, 0, 4);
        }
    }

    // receive (the original dereferences the slot unconditionally; without one the message is dropped here)
    std::array<std::byte, kPlayerDataSize> buf{};
    if (net_.pop_received(m.steam_id, kPlayerStateChannel, buf.data(), kPlayerStateSize) > 0 && slot) {
        decode_player_state(std::span<const std::byte>(buf.data(), kPlayerStateSize), slot);
        rx_flags_[m.steam_id] |= 4;
    }
    buf.fill(std::byte{0});
    if (net_.pop_received(m.steam_id, kPlayerDataChannel, buf.data(), kPlayerDataSize) > 0 && slot) {
        game_->deserialize(slot, buf.data());
        rx_flags_[m.steam_id] |= 8;
    }

    // send, unless masked by peer+0x38 bit0 / bit1 (C, rx38_* runs)
    const std::uint32_t flags = rx_flags(m.steam_id);
    if (!(flags & 1)) {
        const auto state = encode_player_state(local);
        bus_.send(m.steam_id, state, kPlayerStateChannel);
    }
    if (!(flags & 2)) {
        buf.fill(std::byte{0});
        game_->serialize(local, buf.data());
        bus_.send(m.steam_id, buf, kPlayerDataChannel);
    }
}

}  // namespace nrsc::session
