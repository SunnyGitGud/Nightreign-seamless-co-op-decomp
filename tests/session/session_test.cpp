// Session layer (src/nrsc/session, docs/session.md) with mocked Steam: the behaviour tests of phase3 section 3
// (timer rule, handshake state machine, counters, LobbyDataUpdate table, member count) plus join/leave attach/detach
// and the party word.
#include <gtest/gtest.h>

#include <cstring>
#include <limits>
#include <map>
#include <thread>
#include <vector>

#include "nrsc/hooks/net/net_group.hpp"
#include "nrsc/session/session.hpp"

using namespace nrsc;
using namespace nrsc::session;
using server::SteamId;

namespace {

struct MockMM : server::ISteamMatchmaking {
    std::map<std::pair<SteamId, std::string>, std::string> data;
    std::vector<SteamId> members;
    SteamId owner = 0;
    std::int32_t GetNumLobbyMembers(SteamId) override { return static_cast<std::int32_t>(members.size()); }
    SteamId GetLobbyMemberByIndex(SteamId, std::int32_t i) override { return members.at(static_cast<std::size_t>(i)); }
    std::string GetLobbyData(SteamId l, const std::string& k) override {
        auto it = data.find({l, k});
        return it == data.end() ? "" : it->second;
    }
    bool SetLobbyData(SteamId l, const std::string& k, const std::string& v) override { data[{l, k}] = v; return true; }
    SteamId GetLobbyOwner(SteamId) override { return owner; }
    void make_seamless(SteamId l, const std::string& version = "1.1.3") {
        data[{l, "nrsc_lobby_type"}] = "nrsc_ynx3_seamless_master_lobby";
        data[{l, "nrsc_lobby_version"}] = version;
    }
};

struct Sent { SteamId to; std::vector<std::byte> data; };
struct MockNet : server::ISteamNetworkingMessages {
    std::vector<Sent> sent;
    std::vector<server::ReceivedMessage> inbox;
    std::vector<SteamId> accepted;
    int SendMessageToUser(SteamId t, std::span<const std::byte> d, int, int) override {
        sent.push_back({t, {d.begin(), d.end()}});
        return server::kResultOk;
    }
    std::vector<server::ReceivedMessage> ReceiveMessagesOnChannel(int, int) override { return std::exchange(inbox, {}); }
    bool AcceptSessionWithUser(SteamId r) override { accepted.push_back(r); return true; }
    bool CloseSessionWithUser(SteamId r) override { closed.push_back(r); return true; }
    std::vector<SteamId> closed;
    // control packets sent: id 3 channel 1
    std::size_t controls() const {
        std::size_t n = 0;
        for (auto& s : sent) if (s.data.size() == 8 && s.data[0] == std::byte{3} && s.data[1] == std::byte{1}) ++n;
        return n;
    }
};

struct MockLobbies : ISteamLobbyControl {
    std::vector<std::pair<int, int>> created;
    std::vector<SteamId> joined, left, requested;
    bool request_ok = true;
    void CreateLobby(int t, int m) override { created.emplace_back(t, m); }
    void JoinLobby(SteamId l) override { joined.push_back(l); }
    void LeaveLobby(SteamId l) override { left.push_back(l); }
    bool RequestLobbyData(SteamId l) override { requested.push_back(l); return request_ok; }
};

struct MockInfo : ISteamSessionInfo {
    std::map<SteamId, std::int32_t> ping;
    std::vector<SteamId> asked;
    bool GetSessionConnectionInfo(SteamId r, std::int32_t& p) override { asked.push_back(r); p = ping[r]; return true; }
};

constexpr SteamId kMe = 100, kHost = 200, kGuest = 300, kLobby = 5000, kFriend = 77;

server::ReceivedMessage control_packet(SteamId from, std::uint8_t bit) {
    server::ReceivedMessage m;
    m.steam_id = from;
    m.data = {std::byte{3}, std::byte{1}, std::byte{0}, std::byte{1}, std::byte{0}, std::byte{0}, std::byte{0}, std::byte{bit}};
    return m;
}

struct Fixture : ::testing::Test {
    MockMM mm; MockNet net; MockLobbies lobbies; MockInfo info;
    hooks::net::NetState ns;
    std::vector<std::uint32_t> party_calls;  // session words written (only on change)
    std::unique_ptr<Session> s;
    void SetUp() override {
        SessionConfig cfg;
        cfg.handshake_rate = 0.0f;  // keep the resend timer out of the way unless a test sets it
        cfg.ping_rate = 0.0f;
        s = std::make_unique<Session>(SteamInterfaces{mm, net, lobbies, info}, kMe, ns,
                                      [this](std::uint32_t w) { party_calls.push_back(w); }, cfg);
    }
    // Join lobby kLobby (members: me, host, guest) through the full callback flow.
    void join_lobby() {
        mm.make_seamless(kLobby);
        mm.members = {kMe, kHost, kGuest};
        mm.owner = kHost;
        s->on_game_lobby_join_requested(kLobby, kFriend);
        s->on_lobby_data_update(kLobby, kLobby, true);
        s->on_lobby_enter(kLobby);
    }
};

}  // namespace

// ---- (a) timer rule ----
TEST(SessionTimer, ResetsIffAccAtLeastInverseRate) {
    float acc = 0;
    EXPECT_FALSE(timer_fires(acc, 2.0f, 0.4f));   // 0.4 < 0.5
    EXPECT_TRUE(timer_fires(acc, 2.0f, 0.1f));    // 0.5 >= 0.5
    EXPECT_EQ(acc, 0.0f);
    acc = 0;
    EXPECT_TRUE(timer_fires(acc, 0.6667f, 1.5f)); // the old "2/3" point: 1.5 s
    acc = 0;
    EXPECT_FALSE(timer_fires(acc, 0.6667f, 1.4f));
    acc = 0;
    EXPECT_FALSE(timer_fires(acc, 0.0f, 1000.0f));  // rate 0: never
    acc = 0;
    EXPECT_TRUE(timer_fires(acc, -1.0f, 0.0f));     // rate < 0: always
}

// ---- JoinFlow, phase3 1.3 ----
struct JoinFlowTest : ::testing::Test {
    MockMM mm; SessionState st; JoinFlow jf{st, mm};
    void pending(SteamId lobby = kLobby) { jf.on_game_lobby_join_requested(lobby, kFriend); }
};

TEST_F(JoinFlowTest, JoinRequestedStoresTargetAndSetsPending) {
    pending();
    EXPECT_EQ(st.join_lobby(), kLobby);
    EXPECT_EQ(st.join_friend(), kFriend);
    EXPECT_TRUE(st.has(kJoinPending));
}
TEST_F(JoinFlowTest, LobbyMismatchIgnoredNoWrite) {
    pending();
    st.set(kInLobby);
    EXPECT_EQ(jf.on_lobby_data_update(kLobby + 1, 0, true), JoinFlow::DataResult::Ignored);
    EXPECT_EQ(st.join_lobby(), kLobby);
    EXPECT_TRUE(st.has(kJoinPending));
}
TEST_F(JoinFlowTest, FlagClearIgnored) {
    pending();
    st.clear(kJoinPending);
    EXPECT_EQ(jf.on_lobby_data_update(kLobby, 0, true), JoinFlow::DataResult::Ignored);
    EXPECT_EQ(st.join_lobby(), kLobby);
}
TEST_F(JoinFlowTest, FailureClearsTargetKeepsErrorWord) {
    pending();
    st.set(kInLobby);
    st.join_error = 0x1234;
    EXPECT_EQ(jf.on_lobby_data_update(kLobby, 0, false), JoinFlow::DataResult::BadData);
    EXPECT_EQ(st.join_lobby(), 0u);
    EXPECT_EQ(st.join_friend(), 0u);
    EXPECT_FALSE(st.has(kJoinPending));
    EXPECT_TRUE(st.has(kInLobby));  // other bits kept
    EXPECT_EQ(st.join_error, 0x1234u);
}
TEST_F(JoinFlowTest, NotSeamlessAndWrongVersion) {
    pending();
    mm.data[{kLobby, "nrsc_lobby_type"}] = "something_else";
    EXPECT_EQ(jf.on_lobby_data_update(kLobby, 0, true), JoinFlow::DataResult::NoSeamless);
    EXPECT_EQ(st.join_error, kJoinErrorNoSeamless);
    EXPECT_EQ(st.join_lobby(), 0u);

    pending();
    mm.make_seamless(kLobby, "1.1.2");
    EXPECT_EQ(jf.on_lobby_data_update(kLobby, 0, true), JoinFlow::DataResult::WrongVersion);
    EXPECT_EQ(st.join_error, kJoinErrorWrongVersion);
    EXPECT_EQ(st.join_friend(), 0u);
}
TEST_F(JoinFlowTest, MatchKeepsTargetClearsPendingMemberIgnored) {
    pending();
    mm.make_seamless(kLobby);
    EXPECT_EQ(jf.on_lobby_data_update(kLobby, 0xdeadbeef, true), JoinFlow::DataResult::Accepted);
    EXPECT_EQ(st.join_lobby(), kLobby);
    EXPECT_EQ(st.join_friend(), kFriend);
    EXPECT_FALSE(st.has(kJoinPending));
    EXPECT_EQ(st.join_error, 0u);
}

// ---- FrameTask ----
struct FrameTest : ::testing::Test {
    MockMM mm; MockNet net; MockInfo info; hooks::net::NetState ns;
    SessionState st; server::PeerList peers{server::kPeerPoolSize}; server::MessageBus bus{st.core, peers, net};
    FrameTask ft{st, peers, bus, ns, info};
    FrameTest() { ns.attach(bus, peers); ft.timers().rate1 = ft.timers().rate2 = 0.0f; }  // timers off
    server::Peer& add(std::size_t i, SteamId id, std::uint32_t flags = server::kPeerActive) {
        peers.slot(i) = server::Peer{.steam_id = id, .flags = flags};
        return peers.slot(i);
    }
    void rx(SteamId id, std::uint8_t b) { std::byte x{b}; ns.enqueue_received(id, 1, {&x, 1}); }
};

TEST_F(FrameTest, HandshakePayloadZeroSetsField3cAndRepliesOnce) {
    auto& p = add(0, 11);
    rx(11, 0);
    ft.tick(0.016f);
    EXPECT_EQ(p.flags, 0x11u);
    EXPECT_EQ(p.field3c, 1u);
    EXPECT_EQ(net.controls(), 1u);
    rx(11, 0); rx(11, 1); rx(11, 0xff);  // later messages are drained, no writes, no reply
    ft.tick(0.016f);
    EXPECT_EQ(p.flags, 0x11u);
    EXPECT_EQ(net.controls(), 1u);
    EXPECT_EQ(ns.queued(11, 1), 0u);
}
TEST_F(FrameTest, HandshakePayloadOneSetsRemoteBit3NotField3c) {
    auto& p = add(0, 11);
    rx(11, 1);
    ft.tick(0.016f);
    EXPECT_EQ(p.flags, 0x51u);
    EXPECT_EQ(p.field3c, 0u);
    EXPECT_EQ(net.controls(), 1u);
}
TEST_F(FrameTest, InactivePeerAndOtherChannelNotReceived) {
    auto& p = add(0, 11, 0);
    rx(11, 0);
    ft.tick(0.016f);
    EXPECT_EQ(p.flags, 0u);
    EXPECT_EQ(net.controls(), 0u);
    auto& q = add(1, 12, 0x21);  // host bit kept
    std::byte x{0};
    ns.enqueue_received(12, 2, {&x, 1});
    ft.tick(0.016f);
    EXPECT_EQ(q.flags, 0x21u);
}
TEST_F(FrameTest, ResendOnlyToActiveNotHandshaken) {
    ft.timers().rate1 = 1.0f;
    add(0, 11, 0x01); add(1, 12, 0x11); add(2, 13, 0x21); add(3, 14, 0x00);
    ft.tick(0.5f);
    EXPECT_EQ(net.controls(), 0u);
    ft.tick(0.5f);  // acc 1.0 >= 1/1
    ASSERT_EQ(net.controls(), 2u);  // 0x01 and 0x21
    EXPECT_EQ(net.sent[0].to, 11u);
    EXPECT_EQ(net.sent[1].to, 13u);
}
TEST_F(FrameTest, ControlCarriesBit3) {
    st.set(kBit3);
    add(0, 11);
    rx(11, 0);
    ft.tick(0.016f);
    ASSERT_EQ(net.sent.size(), 1u);
    EXPECT_EQ(net.sent[0].data[7], std::byte{1});
}
TEST_F(FrameTest, PingPollOnlyHandshaken) {
    ft.timers().rate2 = 2.0f;
    add(0, 11, 0x11); add(1, 12, 0x51); add(2, 13, 0x01);
    info.ping[11] = 40; info.ping[12] = 90;
    ft.tick(0.5f);
    EXPECT_EQ(info.asked, (std::vector<SteamId>{11, 12}));
    EXPECT_EQ(ft.ping_ms(11), 40);
    EXPECT_EQ(ft.ping_ms(12), 90);
    EXPECT_EQ(ft.ping_ms(13), -1);
}
TEST_F(FrameTest, CountersSplitByRemoteBit3AndSkipInactive) {
    add(0, 11, 0x01); add(1, 12, 0x11); add(2, 13, 0x51); add(3, 14, 0x41); add(4, 15, 0x00);
    ft.tick(0.0f);
    EXPECT_EQ(ft.active_without_bit3(), 2);
    EXPECT_EQ(ft.active_with_bit3(), 2);
    peers.slot(1).flags = 0;
    ft.tick(0.0f);  // recomputed every frame
    EXPECT_EQ(ft.active_without_bit3(), 1);
}
TEST_F(FrameTest, MemberCountIsBytesOver0xF0) {
    ft.tick(0.0f);
    EXPECT_EQ(ft.member_count(), 0);  // no source = empty
    std::size_t bytes = 0;
    ft.set_member_bytes_source([&] { return bytes; });
    bytes = 3 * 0xF0; ft.tick(0.0f); EXPECT_EQ(ft.member_count(), 3);
    bytes = 0xEF; ft.tick(0.0f); EXPECT_EQ(ft.member_count(), 0);
    bytes = 0; ft.tick(0.0f); EXPECT_EQ(ft.member_count(), 0);
}
TEST_F(FrameTest, OneJobPerCall) {
    int n = 0;
    ft.post_job([&] { ++n; }); ft.post_job([&] { ++n; });
    EXPECT_TRUE(ft.run_one_job()); EXPECT_EQ(n, 1);
    EXPECT_TRUE(ft.run_one_job()); EXPECT_FALSE(ft.run_one_job()); EXPECT_EQ(n, 2);
}

// ---- Session: join / leave / attach ----
TEST_F(Fixture, JoinRequestRequestsDataThenJoinsOnlyWhenAccepted) {
    mm.make_seamless(kLobby, "9.9.9");
    s->on_game_lobby_join_requested(kLobby, kFriend);
    EXPECT_EQ(lobbies.requested, (std::vector<SteamId>{kLobby}));
    s->on_lobby_data_update(kLobby, kLobby, true);
    EXPECT_TRUE(lobbies.joined.empty());
    EXPECT_EQ(s->join_error(), kJoinErrorWrongVersion);

    mm.make_seamless(kLobby);
    s->on_game_lobby_join_requested(kLobby, kFriend);
    s->on_lobby_data_update(kLobby, kLobby, true);
    EXPECT_EQ(lobbies.joined, (std::vector<SteamId>{kLobby}));
    EXPECT_FALSE(ns.attached());  // not until LobbyEnter
}
TEST_F(Fixture, LobbyEnterAttachesAndLeaveDetaches) {
    EXPECT_FALSE(ns.attached());
    join_lobby();
    EXPECT_TRUE(s->in_lobby());
    EXPECT_TRUE(ns.attached());
    EXPECT_EQ(ns.bus, &s->bus());
    EXPECT_EQ(ns.peers, &s->peers());
    EXPECT_EQ(s->peers().size(), server::kPeerPoolSize);
    EXPECT_EQ(s->state().join_lobby(), 0u);  // join target consumed
    EXPECT_TRUE(s->peers().find_active(kHost)->host());
    EXPECT_NE(s->peers().find_active(kGuest), nullptr);
    EXPECT_EQ(s->peers().find_active(kMe), nullptr);

    s->leave_session();
    EXPECT_EQ(lobbies.left, (std::vector<SteamId>{kLobby}));
    EXPECT_FALSE(ns.attached());
    EXPECT_FALSE(s->in_lobby());
    EXPECT_EQ(s->peers().find_active(kHost), nullptr);
}
TEST_F(Fixture, FailedOrForeignLobbyEnterDoesNotAttach) {
    mm.make_seamless(kLobby);
    s->on_game_lobby_join_requested(kLobby, kFriend);
    EXPECT_FALSE(s->on_lobby_enter(kLobby, 5));  // e.g. k_EChatRoomEnterResponseFull
    EXPECT_FALSE(ns.attached());
    EXPECT_EQ(s->state().join_lobby(), 0u);
    EXPECT_FALSE(s->on_lobby_enter(999));        // data of 999 is not a seamless lobby
    EXPECT_FALSE(ns.attached());
}
TEST_F(Fixture, JoinRequestWithUnsendableDataRequestDropsTarget) {
    lobbies.request_ok = false;
    s->on_game_lobby_join_requested(kLobby, kFriend);
    EXPECT_EQ(s->state().join_lobby(), 0u);
    EXPECT_EQ(s->flags() & kJoinPending, 0u);
}
TEST_F(Fixture, HostFlowCreatesLobbyTagsDataAndAttachesOnEnter) {
    EXPECT_TRUE(s->host_start_session());
    EXPECT_FALSE(s->host_start_session());  // already pending
    ASSERT_EQ(lobbies.created.size(), 1u);
    s->on_lobby_created(kLobby);
    EXPECT_EQ(mm.GetLobbyData(kLobby, "nrsc_lobby_type"), "nrsc_ynx3_seamless_master_lobby");
    EXPECT_EQ(mm.GetLobbyData(kLobby, "nrsc_lobby_version"), "1.1.3");
    EXPECT_NE(s->flags() & kLobbyCreated, 0u);
    mm.members = {kMe}; mm.owner = kMe;
    EXPECT_TRUE(s->on_lobby_enter(kLobby));
    EXPECT_TRUE(ns.attached());
    EXPECT_FALSE(s->host_start_session());  // in a lobby
    s->leave_session();
    EXPECT_EQ(s->flags() & (kInLobby | kLobbyCreated), 0u);
}
// The lobby trigger (devirt 2.11): the game's CreateLobby through the vtable hook.
TEST_F(Fixture, GamePublicCreateLobbyBecomesFriendsOnlySeamlessLobby) {
    EXPECT_EQ(s->on_game_create_lobby(kLobbyTypePublic), kLobbyTypeFriendsOnly);
    EXPECT_TRUE(s->state().core.lobby_create_pending);
    EXPECT_TRUE(lobbies.created.empty());  // the game's own call creates it, not the session
    s->on_lobby_created(kLobby);
    EXPECT_EQ(mm.GetLobbyData(kLobby, "nrsc_lobby_type"), "nrsc_ynx3_seamless_master_lobby");
    EXPECT_NE(s->flags() & kLobbyCreated, 0u);
}
TEST_F(Fixture, GameNonPublicCreateLobbyPassesThrough) {
    for (int t : {0, 1, 3}) EXPECT_EQ(s->on_game_create_lobby(t), t);
    EXPECT_FALSE(s->state().core.lobby_create_pending);
    s->on_lobby_created(kLobby);
    EXPECT_EQ(mm.GetLobbyData(kLobby, "nrsc_lobby_type"), "");  // not tagged
}
TEST_F(Fixture, GameLeaveOfCurrentLobbyClosesPeerSessionsAndDetaches) {
    join_lobby();
    s->on_game_leave_lobby(999);  // another lobby: nothing
    EXPECT_TRUE(ns.attached());
    EXPECT_TRUE(net.closed.empty());
    s->on_game_leave_lobby(kLobby);
    EXPECT_FALSE(ns.attached());
    EXPECT_FALSE(s->in_lobby());
    EXPECT_EQ(net.closed.size(), 2u);  // host and guest
    EXPECT_TRUE(lobbies.left.empty());  // the game's own LeaveLobby follows; the session does not call it again
}
TEST_F(Fixture, ChatUpdateJoinAndLeave) {
    join_lobby();
    s->on_lobby_chat_update(kLobby, 400, server::kChatEntered);
    EXPECT_NE(s->peers().find_active(400), nullptr);
    s->on_lobby_chat_update(kLobby, 400, server::kChatLeft);
    EXPECT_EQ(s->peers().find_active(400), nullptr);
}
TEST_F(Fixture, SimpleCallbacks) {
    s->on_relay_network_status(0x66);
    EXPECT_EQ(s->state().core.relay_status, 0x66u);
    s->on_session_request(kHost);
    EXPECT_EQ(net.accepted, (std::vector<SteamId>{kHost}));
    s->on_session_failed();
    s->state().core.closed = true;
    EXPECT_THROW(s->on_session_request(kHost), std::system_error);
}
TEST_F(Fixture, StartupHandlersRegisteredAndPeerChannelFeedsNetState) {
    join_lobby();
    net.inbox = {control_packet(kHost, 1)};
    EXPECT_EQ(s->bus().poll(), 1);
    EXPECT_EQ(ns.queued(kHost, 1), 1u);
    std::byte b{};
    EXPECT_EQ(ns.pop_received(kHost, 1, &b, 1), 1u);
    EXPECT_EQ(b, std::byte{1});
}

// ---- Session: party word ----
TEST_F(Fixture, PartyWordSetOnHandshakeAndClearedOnLeave) {
    join_lobby();
    EXPECT_EQ(party_calls, (std::vector<std::uint32_t>{0x01}));  // guest in lobby, nobody handshaken yet
    net.inbox = {control_packet(kHost, 0)};
    s->tick(0.016f);                    // poll -> queue -> handshake -> party
    EXPECT_TRUE(s->party_active());
    EXPECT_EQ(party_calls, (std::vector<std::uint32_t>{0x01, 0x21}));
    EXPECT_NE(s->flags() & kParty, 0u);
    EXPECT_EQ(s->peers().find_active(kHost)->flags & 0x31u, 0x31u);
    EXPECT_EQ(net.controls(), 1u);
    s->tick(0.016f);
    EXPECT_EQ(party_calls.size(), 2u);  // only on change

    s->leave_session();
    EXPECT_FALSE(s->party_active());
    EXPECT_EQ(party_calls, (std::vector<std::uint32_t>{0x01, 0x21, 0}));
    EXPECT_EQ(s->flags() & kParty, 0u);
}
TEST_F(Fixture, PartyEndsWhenLastHandshakenPeerLeaves) {
    join_lobby();
    net.inbox = {control_packet(kHost, 0)};
    s->tick(0.016f);
    ASSERT_TRUE(s->party_active());
    s->on_lobby_chat_update(kLobby, kHost, server::kChatDisconnected);
    EXPECT_FALSE(s->party_active());
    ASSERT_GE(party_calls.size(), 3u);
    EXPECT_EQ(party_calls[1], 0x21u);
    EXPECT_EQ(party_calls.back() & 0x20u, 0u);  // bit 5 gone with the last handshaken peer
    EXPECT_EQ(s->ping_ms(kHost), -1);
}
TEST_F(Fixture, TickIsInertWhileDetached) {
    s->post_job([] {});
    net.inbox = {control_packet(kHost, 0)};
    s->tick(10.0f);
    EXPECT_EQ(net.inbox.size(), 1u);  // not polled
    EXPECT_TRUE(party_calls.empty());
}
TEST_F(Fixture, JobsRunOutsideLockAndMayCallSession) {
    int n = 0;
    s->post_job([&] { ++n; (void)s->in_lobby(); });  // would deadlock if run under the mutex
    s->tick(0.0f);
    EXPECT_EQ(n, 1);
}
// Host side (live session record 2026-10-16): 0x05 from lobby entry, never bit 5, also with a handshaken guest.
TEST_F(Fixture, HostWordIsLobbyEnteredAndCreatedWithoutBit5) {
    ASSERT_TRUE(s->host_start_session());
    s->on_lobby_created(kLobby);
    mm.members = {kMe, kGuest}; mm.owner = kMe;
    ASSERT_TRUE(s->on_lobby_enter(kLobby));
    EXPECT_EQ(party_calls, (std::vector<std::uint32_t>{0x05}));
    net.inbox = {control_packet(kGuest, 0)};
    s->tick(0.016f);
    EXPECT_TRUE(s->party_active());              // a handshaken peer exists
    EXPECT_EQ(s->flags() & kParty, 0u);          // but no bit 5 on the host
    EXPECT_EQ(party_calls, (std::vector<std::uint32_t>{0x05}));
    s->leave_session();
    EXPECT_EQ(party_calls, (std::vector<std::uint32_t>{0x05, 0}));
}
TEST(SessionWord, DefaultSetterDrivesTheRealPartyWord) {
    MockMM mm; MockNet net; MockLobbies lobbies; MockInfo info;
    auto& real = hooks::net::net_state();
    Session s({mm, net, lobbies, info}, kMe, real);
    mm.make_seamless(kLobby); mm.members = {kMe, kHost}; mm.owner = kHost;
    s.on_lobby_enter(kLobby);
    net.inbox = {control_packet(kHost, 0)};
    s.tick(0.016f);
    std::uint32_t w;
    std::memcpy(&w, real.state_blob.data(), 4);
    EXPECT_EQ(w & 0x21u, 0x21u);
    s.leave_session();
    std::memcpy(&w, real.state_blob.data(), 4);
    EXPECT_EQ(w & 0x21u, 0u);
    EXPECT_FALSE(real.attached());
}
TEST_F(Fixture, DestructorDetaches) {
    join_lobby();
    s.reset();
    EXPECT_FALSE(ns.attached());
}
TEST_F(Fixture, ConcurrentTickAndPostJobAreSafe) {
    join_lobby();
    std::atomic<int> ran{0};
    std::thread t([&] { for (int i = 0; i < 200; ++i) s->post_job([&] { ++ran; }); });
    for (int i = 0; i < 300; ++i) { s->tick(0.001f); (void)s->in_lobby(); }
    t.join();
    while (ran < 200) s->tick(0.0f);
    EXPECT_EQ(ran, 200);
}

// ---- party data sync + F4 pause (PlayerSync, docs/devirt.md 2.12). Expected bytes are the emulation's. ----
namespace sync_test {

struct FakeGame : IGamePlayers {
    std::vector<std::byte> local = std::vector<std::byte>(0x900);
    std::vector<std::byte> table = std::vector<std::byte>(4 * kRemotePlayerStride);
    std::vector<PartyMember> party;
    std::optional<std::uint32_t> state = 0u;
    std::vector<std::pair<std::byte*, std::vector<std::byte>>> applied;
    std::vector<bool> keys;
    std::vector<std::u16string> shown;
    bool world = true;
    bool world_ready() override { return world; }
    std::optional<std::uint32_t> session_state() override { return state; }
    std::vector<PartyMember> members() override { return party; }
    std::byte* local_player_data() override { return local.data(); }
    std::byte* remote_player_table() override { return table.data(); }
    void serialize(std::byte*, std::byte* out) override { for (std::size_t i = 0; i < kPlayerDataSize; ++i) out[i] = std::byte(i * 7 + 3); }
    void deserialize(std::byte* pgd, const std::byte* in) override { applied.push_back({pgd, {in, in + kPlayerDataSize}}); }
    bool f4_pressed() override {
        if (keys.empty()) return false;
        const bool k = keys.front();
        keys.erase(keys.begin());
        return k;
    }
    void show_message(const std::u16string& t) override { shown.push_back(t); }
};

template <class T>
void put(std::vector<std::byte>& b, std::size_t off, T v) { std::memcpy(b.data() + off, &v, sizeof v); }

}  // namespace sync_test

TEST(PlayerSyncCodec, EncodeMatchesTheTaintRun) {
    std::vector<std::byte> pgd(0x900);
    using sync_test::put;
    put<std::uint32_t>(pgd, 0x54, 0x11110011); put<std::uint32_t>(pgd, 0x68, 0x11110122); put<std::uint32_t>(pgd, 0xc8, 0x11110233);
    put<std::uint32_t>(pgd, 0xcc, 0x11110344); put<std::uint32_t>(pgd, 0xe8, 0x11110455);
    const std::pair<std::size_t, std::uint8_t> bytes[] = {{0xbe, 0xa1}, {0xc2, 0xa2}, {0xe5, 0xa3}, {0xe6, 0xa4}, {0x107, 0xa5},
                                                         {0x187, 0xa6}, {0x86d, 0xa7}, {0x870, 0xa8}, {0x871, 0xa9}};
    for (auto [o, v] : bytes) pgd[o] = std::byte(v);
    const auto p = encode_player_state(pgd.data());
    const std::uint8_t want[] = {0x55, 0x04, 0x11, 0x11, 0x22, 0x01, 0x11, 0x11, 0x33, 0x02, 0x11, 0x11, 0x44, 0x03,
                                 0x11, 0x11, 0x11, 0x00, 0x11, 0x11, 0xa4, 0xa2, 0xa3, 0xa5, 0x02, 0xe7, 0xa6, 0xa9};
    for (std::size_t i = 0; i < kPlayerStateSize; ++i) EXPECT_EQ(std::to_integer<int>(p[i]), want[i]) << i;
}

TEST(PlayerSyncCodec, DecodeMatchesTheInjectionRun) {
    std::array<std::byte, kPlayerStateSize> payload{};
    for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = std::byte(0x30 + i);
    std::vector<std::byte> pgd(0x900);
    decode_player_state(payload, pgd.data());
    auto u32 = [&](std::size_t o) { std::uint32_t v; std::memcpy(&v, pgd.data() + o, 4); return v; };
    EXPECT_EQ(u32(0xe8), 0x33323130u);
    EXPECT_EQ(u32(0x68), 0x37363534u);
    EXPECT_EQ(u32(0xc8), 0x3b3a3938u);
    EXPECT_EQ(u32(0xcc), 0x3f3e3d3cu);
    EXPECT_EQ(u32(0x54), 0x43424140u);
    EXPECT_EQ(u32(0x58), 0x43424140u);  // mirrored
    EXPECT_EQ(std::to_integer<int>(pgd[0xe6]), 0x44);
    EXPECT_EQ(std::to_integer<int>(pgd[0xc2]), 0x45);
    EXPECT_EQ(std::to_integer<int>(pgd[0xe5]), 0x46);
    EXPECT_EQ(std::to_integer<int>(pgd[0x107]), 0x47);
    EXPECT_EQ(std::to_integer<int>(pgd[0x86d]), 1);  // 0x49: bit0
    EXPECT_EQ(std::to_integer<int>(pgd[0xbe]), 0);   // bit1
    EXPECT_EQ(std::to_integer<int>(pgd[0x870]), 0);  // bit2
    EXPECT_EQ(std::to_integer<int>(pgd[0x187]), 0x4a);
    EXPECT_EQ(std::to_integer<int>(pgd[0x871]), 0x4b);
}

TEST_F(Fixture, PlayerSyncSendsBothBlocksToEveryActiveMemberEachTick) {
    sync_test::FakeGame g;
    g.party = {{kHost, 1}, {kGuest, 2}, {999, 3}};  // 999 is no peer: nothing
    join_lobby();
    s->set_game_players(&g, nullptr);
    net.sent.clear();
    s->tick(0.016f);
    std::map<std::pair<SteamId, int>, std::size_t> sizes;
    for (auto& m : net.sent)
        if (m.data.size() > 8 && m.data[0] == std::byte{3}) sizes[{m.to, std::to_integer<int>(m.data[1])}] = m.data.size();
    EXPECT_EQ(sizes.size(), 4u);
    EXPECT_EQ((sizes[{kHost, 3}]), 7 + kPlayerStateSize);
    EXPECT_EQ((sizes[{kHost, 4}]), 7 + kPlayerDataSize);
    EXPECT_EQ((sizes[{kGuest, 4}]), 7 + kPlayerDataSize);
    // a null session manager does not block (C)
    g.state = std::nullopt;
    net.sent.clear();
    s->tick(0.016f);
    EXPECT_GE(net.sent.size(), 4u);
    // no world object: nothing
    g.state = 0;
    g.world = false;
    net.sent.clear();
    s->tick(0.016f);
    for (auto& m : net.sent) EXPECT_FALSE(m.data.size() > 8 && m.data[0] == std::byte{3} && m.data[1] != std::byte{1});
    g.world = true;
    // state >= 5: nothing is sent
    g.state = 5;
    net.sent.clear();
    s->tick(0.016f);
    for (auto& m : net.sent) EXPECT_FALSE(m.data.size() > 8 && m.data[0] == std::byte{3} && m.data[1] != std::byte{1});
}

TEST_F(Fixture, PlayerSyncAppliesReceivedBlocksToTheMembersSlot) {
    sync_test::FakeGame g;
    g.party = {{kHost, 1}};
    sync_test::put<std::uint32_t>(g.table, kRemotePlayerStride + 0x8, 0xdead);
    join_lobby();
    s->set_game_players(&g, nullptr);
    std::vector<std::byte> st(kPlayerStateSize, std::byte{0x11}), data(kPlayerDataSize, std::byte{0x22});
    ns.enqueue_received(kHost, kPlayerStateChannel, st);
    ns.enqueue_received(kHost, kPlayerDataChannel, data);
    s->tick(0.016f);
    std::byte* slot = g.table.data() + kRemotePlayerStride;  // slot index 1
    std::uint32_t cleared;
    std::memcpy(&cleared, slot + 0x8, 4);
    EXPECT_EQ(cleared, 0u);
    EXPECT_EQ(std::to_integer<int>(slot[0xe6]), 0x11);
    ASSERT_EQ(g.applied.size(), 1u);
    EXPECT_EQ(g.applied[0].first, slot);
    EXPECT_EQ(s->player_sync().rx_flags(kHost), 12u);
}

TEST_F(Fixture, F4TogglesThePauseByteAndShowsTheMessage) {
    sync_test::FakeGame g;
    g.keys = {true, false, true};
    join_lobby();
    s->set_game_players(&g, [](std::int32_t c, std::int32_t id) -> std::optional<std::u16string> {
        if (c != 15000) return std::nullopt;
        return id == 2 ? std::u16string(u"Paused") : std::u16string(u"Resumed");
    });
    s->tick(0.016f);
    EXPECT_TRUE(s->paused());
    EXPECT_EQ(ns.state_blob[hooks::net::NetState::kStateForceFlag], 1);
    s->tick(0.016f);
    EXPECT_TRUE(s->paused());
    s->tick(0.016f);
    EXPECT_FALSE(s->paused());
    EXPECT_EQ(ns.state_blob[hooks::net::NetState::kStateForceFlag], 0);
    EXPECT_EQ(g.shown, (std::vector<std::u16string>{u"Paused", u"Resumed"}));
}

TEST_F(Fixture, PeerFlagBitsMaskTheTwoSends) {
    sync_test::FakeGame g;
    g.party = {{kHost, 1}};
    join_lobby();
    s->set_game_players(&g, nullptr);
    auto count = [&](int ch) {
        std::size_t n = 0;
        for (auto& m : net.sent)
            if (m.to == kHost && m.data.size() > 8 && m.data[0] == std::byte{3} && std::to_integer<int>(m.data[1]) == ch) ++n;
        return n;
    };
    for (std::uint32_t f : {1u, 2u, 3u, 0xcu}) {
        s->player_sync().set_peer_flags(kHost, f);
        net.sent.clear();
        s->tick(0.016f);
        EXPECT_EQ(count(3), (f & 1) ? 0u : 1u) << f;
        EXPECT_EQ(count(4), (f & 2) ? 0u : 1u) << f;
    }
}
