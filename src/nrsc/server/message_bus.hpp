#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <vector>

#include "nrsc/server/core_state.hpp"
#include "nrsc/server/peer.hpp"
#include "nrsc/server/steam_iface.hpp"

// Steam P2P message layer of server_manager (docs/server_manager.md 2.2).
namespace nrsc::server {

inline constexpr std::uint8_t kMsgData = 0x03;          // C: the only readable message id (0x18003f0e2)
inline constexpr std::uint8_t kControlChannel = 0x01;   // C: 0x18003f30f stores the word 0x0103
inline constexpr int kMaxPollMessages = 256;            // C: 0x180063195 (r9d=0x100) and the guard at 0x180063206

// handler(sender, payload (whole packet incl. header, as received), remaining = size - 7). The original passes
// (&ctx, &data, &peer, &remaining) through a std::function stored in a std::map<u8, ...> at ctx+0x78 (C).
using MessageHandler = std::function<void(Peer& sender, std::span<const std::byte> packet, std::uint32_t remaining)>;

class MessageBus {
public:
    MessageBus(CoreState& core, PeerList& peers, ISteamNetworkingMessages& net)
        : core_(core), peers_(peers), net_(net) {}

    // Handler registration happens in protected code (0x180063570 has no readable caller): the registration API is
    // mine, the map is C. Replaces an existing handler for the id.
    void register_handler(std::uint8_t id, MessageHandler handler) { handlers_[id] = std::move(handler); }

    // Channels listed here are sent unreliably (flags 5), all others reliably (flags 9). This is the int vector at
    // ctx+0x48..+0x50 that 0x18003f030 searches with std::find (0x1800901a0) for the channel argument. The contents
    // are filled by protected code (U). docs/server_manager.md describes the flag choice as "target == current host";
    // the disassembly shows a channel lookup instead (see report).
    std::vector<std::int32_t>& unreliable_channels() { return unreliable_channels_; }

    // 0x180063110 (C). Receives up to 256 messages on core.channel. For each: find an active peer by sender (identity
    // type 0x10 uses the SteamID, any other type matches id 0); drop if none; drop if size < 8; drop if flag == 0 and
    // len > max_len; look the id up in the handler map and silently drop if absent; otherwise call the handler with
    // remaining = size - 7. A registered but empty std::function throws std::bad_function_call (0x18008f158), as
    // in the original. Returns the number of dispatched messages.
    int poll();

    // 0x18003f030 SendData (C). Looks up the active peer `target`; returns false if none. Packet = [3][channel][0]
    // [u32 len][payload]. Flags 9 unless `channel` is in unreliable_channels(), then 5. Sent on core.channel. Returns
    // true when SendMessageToUser returned k_EResultOK.
    bool send(SteamId target, std::span<const std::byte> payload, std::uint8_t channel);

    // 0x18003f2c0 (C). 8-byte message {03 01 00 01 00 00 00 b}, b = (core.state_flags >> 3) & 1, flags 9, sent to the
    // peer's id (the peer is not looked up here). The result is ignored. In the original the state is read from the
    // core object's first byte.
    void send_control(const Peer& peer);

private:
    CoreState& core_;
    PeerList& peers_;
    ISteamNetworkingMessages& net_;
    std::map<std::uint8_t, MessageHandler> handlers_;
    std::vector<std::int32_t> unreliable_channels_;
};

// 0x180061060 (C structure, S meaning). Run slot of a server_manager task: looks `key` (int at [component+0x68]) up in
// a std::map<int, std::function> and calls it with (&component, &arg). A missing key is fatal:
// FatalError("YuiNexusNetRankingDB", "nrsc\\mod\\server_manager\\server_manager.cpp", 0x9b).
inline constexpr const char* kGameComponentFatalWhat = "YuiNexusNetRankingDB";
inline constexpr const char* kServerManagerSource = "nrsc\\mod\\server_manager\\server_manager.cpp";
inline constexpr unsigned kGameComponentFatalLine = 0x9b;

using GameComponentHandler = std::function<void(int key, void* arg)>;
using GameComponentMap = std::map<int, GameComponentHandler>;

void dispatch_game_component(const GameComponentMap& handlers, int key, void* arg);

}  // namespace nrsc::server
