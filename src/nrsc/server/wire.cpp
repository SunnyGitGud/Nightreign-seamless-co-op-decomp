#include "nrsc/server/wire.hpp"

#include <cstring>

namespace nrsc::server::wire {

std::vector<std::byte> encode(const Header& header, std::span<const std::byte> payload) {
    std::vector<std::byte> out(kHeaderSize + payload.size());
    out[0] = std::byte{header.id};
    out[1] = std::byte{header.channel};
    out[2] = std::byte{header.flag};
    const std::uint32_t len = header.len;
    for (int i = 0; i < 4; ++i) out[3 + i] = std::byte((len >> (8 * i)) & 0xff);
    if (!payload.empty()) std::memcpy(out.data() + kHeaderSize, payload.data(), payload.size());
    return out;
}

std::optional<Header> decode(std::span<const std::byte> packet) {
    if (packet.size() < kHeaderSize) return std::nullopt;
    Header h;
    h.id = std::to_integer<std::uint8_t>(packet[0]);
    h.channel = std::to_integer<std::uint8_t>(packet[1]);
    h.flag = std::to_integer<std::uint8_t>(packet[2]);
    h.len = 0;
    for (int i = 0; i < 4; ++i) h.len |= std::uint32_t(std::to_integer<std::uint8_t>(packet[3 + i])) << (8 * i);
    return h;
}

bool validate(std::span<const std::byte> packet, std::uint32_t max_len) {
    const auto h = decode(packet);
    if (!h) return false;
    if (h->len != packet.size() - kHeaderSize) return false;
    if (h->flag == 0 && h->len > max_len) return false;
    return true;
}

bool poll_accepts(std::span<const std::byte> packet, std::uint32_t max_len) {
    if (packet.size() < kHeaderSize + 1) return false;
    const auto h = decode(packet);
    if (h->flag == 0 && h->len > max_len) return false;
    return true;
}

}  // namespace nrsc::server::wire
