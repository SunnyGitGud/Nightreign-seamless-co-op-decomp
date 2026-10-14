#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

// P2P packet header (docs/server_manager.md 2.2). Layout: id u8, channel u8, flag u8, len u32 (unaligned), then payload.
// Written by 0x18003f030 (SendData) and 0x18003f2c0 (control message); read by the poll 0x180063110 and the
// receivers 0x180040130 / 0x180040240.
namespace nrsc::server::wire {

inline constexpr std::size_t kHeaderSize = 7;

struct Header {
    std::uint8_t id = 0;
    std::uint8_t channel = 0;
    std::uint8_t flag = 0;
    std::uint32_t len = 0;
};

// Header followed by `payload`; len is payload.size(). Grade C (0x18003f0e2..0x18003f0f8, 0x18003f30f..0x18003f31f).
std::vector<std::byte> encode(const Header& header, std::span<const std::byte> payload);

// Little-endian parse of the first 7 bytes; nullopt when fewer than 7 bytes. Grade C.
std::optional<Header> decode(std::span<const std::byte> packet);

// Receiver rule (0x180040130 / 0x180040240; S for the exact receivers, C for the rule as documented):
// size >= 7, len == size - 7, and if flag == 0 then len <= max_len (ctx+0xc).
bool validate(std::span<const std::byte> packet, std::uint32_t max_len);

// Rule applied by the poll itself (0x180063110, C, from disassembly at 0x180063276..0x180063294): size >= 8 and,
// if flag == 0, len <= max_len. It does NOT check len == size - 7.
bool poll_accepts(std::span<const std::byte> packet, std::uint32_t max_len);

}  // namespace nrsc::server::wire
