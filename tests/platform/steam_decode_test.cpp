#include <gtest/gtest.h>

#include <cstring>
#include <vector>

#include "nrsc/platform/steam/steam_callbacks.hpp"

using namespace nrsc::platform::steam;

namespace {
template <typename T>
void put(std::vector<std::byte>& b, std::size_t off, T v) {
    std::memcpy(b.data() + off, &v, sizeof v);
}
struct Rec : ISteamCallbackSink {
    int n = 0;
    LobbyCreatedEvent created; LobbyEnterEvent enter; LobbyChatUpdateEvent chat; GameLobbyJoinRequestedEvent join;
    SessionRequestEvent req; SessionFailedEvent failed; RelayNetworkStatusEvent relay; LobbyDataUpdateEvent data;
    void on_lobby_created(const LobbyCreatedEvent& e) override { created = e; ++n; }
    void on_lobby_enter(const LobbyEnterEvent& e) override { enter = e; ++n; }
    void on_lobby_chat_update(const LobbyChatUpdateEvent& e) override { chat = e; ++n; }
    void on_lobby_data_update(const LobbyDataUpdateEvent& e) override { data = e; ++n; }
    void on_game_lobby_join_requested(const GameLobbyJoinRequestedEvent& e) override { join = e; ++n; }
    void on_session_request(const SessionRequestEvent& e) override { req = e; ++n; }
    void on_session_failed(const SessionFailedEvent& e) override { failed = e; ++n; }
    void on_relay_network_status(const RelayNetworkStatusEvent& e) override { relay = e; ++n; }
};
}  // namespace

TEST(SteamDecode, Identity) {
    auto i = make_steamid_identity(0x0110000100000042ull);
    EXPECT_EQ(i.type, 0x10);
    EXPECT_EQ(i.size, 8);
    EXPECT_EQ(identity_steam_id(i), 0x0110000100000042ull);
    i.type = 1;
    EXPECT_FALSE(identity_steam_id(i));
    std::uint64_t first;  // {type 0x10, size 8} == the 0x800000010 qword built at 0x18003f0fd
    auto j = make_steamid_identity(1);
    std::memcpy(&first, &j, 8);
    EXPECT_EQ(first, 0x800000010ull);
}

TEST(SteamDecode, LobbyEvents) {
    Rec r;
    std::vector<std::byte> b(32);
    put<std::int32_t>(b, 0, 1); put<std::uint64_t>(b, 8, 777);
    EXPECT_TRUE(dispatch_steam_callback(kCbLobbyCreated, b, r));
    EXPECT_EQ(r.created.lobby_id, 777u);
    std::fill(b.begin(), b.end(), std::byte{0});
    put<std::uint64_t>(b, 0, 5); put<std::uint8_t>(b, 12, 1); put<std::uint32_t>(b, 16, 1);
    EXPECT_TRUE(dispatch_steam_callback(kCbLobbyEnter, b, r));
    EXPECT_EQ(r.enter.lobby_id, 5u); EXPECT_TRUE(r.enter.locked); EXPECT_EQ(r.enter.enter_response, 1u);
    put<std::uint64_t>(b, 0, 9); put<std::uint64_t>(b, 8, 10); put<std::uint64_t>(b, 16, 11); put<std::uint32_t>(b, 24, 0x2);
    EXPECT_TRUE(dispatch_steam_callback(kCbLobbyChatUpdate, b, r));
    EXPECT_EQ(r.chat.user_changed, 10u); EXPECT_EQ(r.chat.state_change, 2u);
    EXPECT_TRUE(dispatch_steam_callback(kCbGameLobbyJoinRequested, std::span(b).first(16), r));
    EXPECT_EQ(r.join.lobby_id, 9u); EXPECT_EQ(r.join.friend_id, 10u);
    EXPECT_TRUE(dispatch_steam_callback(kCbLobbyDataUpdate, std::span(b).first(24), r));
    EXPECT_EQ(r.data.lobby_id, 9u);
}

TEST(SteamDecode, NetworkingEvents) {
    Rec r;
    std::vector<std::byte> b(kSessionFailedSize);
    auto id = make_steamid_identity(0xABCD);
    std::memcpy(b.data(), &id, sizeof id);
    EXPECT_TRUE(dispatch_steam_callback(kCbSessionRequest, std::span(b).first(kSessionRequestSize), r));
    EXPECT_TRUE(r.req.is_steam_id); EXPECT_EQ(r.req.steam_id, 0xABCDu);
    put<std::int32_t>(b, 176, 4); put<std::int32_t>(b, 180, 5001);
    EXPECT_TRUE(dispatch_steam_callback(kCbSessionFailed, b, r));
    EXPECT_EQ(r.failed.steam_id, 0xABCDu); EXPECT_EQ(r.failed.state, 4); EXPECT_EQ(r.failed.end_reason, 5001);
    put<std::int32_t>(b, 0, 100);
    EXPECT_TRUE(dispatch_steam_callback(kCbRelayNetworkStatus, std::span(b).first(4), r));
    EXPECT_EQ(r.relay.avail, 100);
}

TEST(SteamDecode, ShortBufferAndUnknownId) {
    Rec r;
    std::vector<std::byte> b(8);
    EXPECT_FALSE(dispatch_steam_callback(kCbLobbyEnter, b, r));
    EXPECT_FALSE(dispatch_steam_callback(0x999, b, r));
    EXPECT_EQ(r.n, 0);
}

TEST(SteamDecode, MessageHeader) {
    std::vector<std::byte> m(216);
    int payload = 0x1234;
    const void* p = &payload;
    put(m, kMsgOffData, p); put<std::int32_t>(m, kMsgOffSize, 4);
    put<std::uint32_t>(m, kMsgOffIdentity, 0x10); put<std::uint64_t>(m, kMsgOffIdentity + 8, 99);
    put<std::int32_t>(m, kMsgOffChannel, 3);
    auto h = decode_message_header(m);
    ASSERT_TRUE(h);
    EXPECT_EQ(h->data, p); EXPECT_EQ(h->size, 4); EXPECT_EQ(h->steam_id, 99u); EXPECT_EQ(h->channel, 3);
    EXPECT_FALSE(decode_message_header(std::span(m).first(100)));
}
