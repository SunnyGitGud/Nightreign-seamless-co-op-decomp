// server_manager startup wiring (docs/devirt.md 2.9): handler table, callback ids and the readable callback bodies.
#include <gtest/gtest.h>

#include <system_error>
#include <vector>

#include "nrsc/server/callbacks.hpp"

using namespace nrsc::server;

namespace {

struct AcceptNet : ISteamNetworkingMessages {
    std::vector<SteamId> accepted;
    std::vector<ReceivedMessage> inbox;
    int SendMessageToUser(SteamId, std::span<const std::byte>, int, int) override { return kResultOk; }
    std::vector<ReceivedMessage> ReceiveMessagesOnChannel(int, int) override { return std::exchange(inbox, {}); }
    bool AcceptSessionWithUser(SteamId remote) override {
        accepted.push_back(remote);
        return true;
    }
};

ReceivedMessage packet(SteamId from, std::uint8_t id) {
    ReceivedMessage m;
    m.steam_id = from;
    m.data = {std::byte(id), std::byte(0), std::byte(1), std::byte(1), std::byte(0), std::byte(0), std::byte(0),
              std::byte(0xaa)};
    return m;
}

}  // namespace

TEST(ServerCallbacks, RegisteredCallbackIdsInOrder) {
    const int expected[] = {0x201, 0x1f8, 0x1fa, 0x4e3, 0x4e4, 0x501, 0x14d, 0x1f9};
    ASSERT_EQ(std::size(kRegisteredCallbacks), std::size(expected));
    for (std::size_t i = 0; i < std::size(expected); ++i)
        EXPECT_EQ(static_cast<int>(kRegisteredCallbacks[i]), expected[i]);
}

TEST(ServerCallbacks, PeerPoolSizeIsFive) { EXPECT_EQ(kPeerPoolSize, 5u); }

TEST(ServerCallbacks, StartupHandlersDispatchByIdAndIgnoreTwo) {
    CoreState core;
    PeerList peers(kPeerPoolSize);
    peers.slot(0) = Peer{.steam_id = 42, .flags = kPeerActive};
    AcceptNet net;
    MessageBus bus(core, peers, net);
    std::vector<int> calls;
    register_startup_handlers(
        bus, [&](Peer&, std::span<const std::byte>, std::uint32_t) { calls.push_back(0); },
        [&](Peer&, std::span<const std::byte>, std::uint32_t) { calls.push_back(1); },
        [&](Peer&, std::span<const std::byte>, std::uint32_t) { calls.push_back(3); });

    net.inbox = {packet(42, 0), packet(42, 1), packet(42, 2), packet(42, 3), packet(42, 4)};
    EXPECT_EQ(bus.poll(), 4);  // id 2 is dispatched to the no-op; id 4 has no handler and is dropped
    EXPECT_EQ(calls, (std::vector<int>{0, 1, 3}));
}

TEST(ServerCallbacks, JoinRequestedStoresLobbyFriendAndFlag) {
    CoreState core;
    core.state_flags = kStateLobbyEntered;
    on_game_lobby_join_requested(core, 0x1100001'00000007ull, 0x1100001'00000009ull);
    EXPECT_EQ(core.join_request_lobby, 0x1100001'00000007ull);
    EXPECT_EQ(core.join_request_friend, 0x1100001'00000009ull);
    EXPECT_EQ(core.state_flags, kStateLobbyEntered | kStateJoinRequested);
}

TEST(ServerCallbacks, RelayStatusStored) {
    CoreState core;
    on_relay_network_status(core, 100);
    EXPECT_EQ(core.relay_status, 100u);
}

TEST(ServerCallbacks, SessionRequestAccepts) {
    CoreState core;
    AcceptNet net;
    on_session_request(core, net, 77);
    EXPECT_EQ(net.accepted, (std::vector<SteamId>{77}));
}

TEST(ServerCallbacks, SessionRequestOnClosedCoreThrows) {
    CoreState core;
    core.closed = true;
    AcceptNet net;
    EXPECT_THROW(on_session_request(core, net, 77), std::system_error);
    EXPECT_TRUE(net.accepted.empty());
}
