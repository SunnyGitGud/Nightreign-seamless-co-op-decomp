// nrsc\mod\server_manager: wire, lobby, message bus, leaderboard, task timer (all against mocks).
#include <gtest/gtest.h>

#include <map>

#include "nrsc/core/fatal_error.hpp"
#include "nrsc/server/leaderboard.hpp"
#include "nrsc/server/lobby.hpp"
#include "nrsc/server/message_bus.hpp"
#include "nrsc/server/task_timer.hpp"
#include "nrsc/server/wire.hpp"

using namespace nrsc::server;
using nrsc::FatalError;

namespace {

std::vector<std::byte> bytes(std::initializer_list<int> v) {
    std::vector<std::byte> out;
    for (int x : v) out.push_back(std::byte(x));
    return out;
}

struct MockMatchmaking : ISteamMatchmaking {
    std::map<std::pair<SteamId, std::string>, std::string> data;
    std::vector<SteamId> members;
    SteamId owner = 0;
    int set_calls = 0;
    int fail_on_call = 0;  // 1-based
    std::int32_t GetNumLobbyMembers(SteamId) override { return std::int32_t(members.size()); }
    SteamId GetLobbyMemberByIndex(SteamId, std::int32_t i) override { return members.at(i); }
    std::string GetLobbyData(SteamId l, const std::string& k) override { return data[{l, k}]; }
    bool SetLobbyData(SteamId l, const std::string& k, const std::string& v) override {
        ++set_calls;
        if (set_calls == fail_on_call) return false;
        data[{l, k}] = v;
        return true;
    }
    SteamId GetLobbyOwner(SteamId) override { return owner; }
};

struct SentMsg {
    SteamId target;
    std::vector<std::byte> data;
    int flags, channel;
};

struct MockNet : ISteamNetworkingMessages {
    std::vector<SentMsg> sent;
    std::vector<ReceivedMessage> inbox;
    int result = kResultOk;
    int last_recv_channel = -1, last_recv_max = -1;
    int SendMessageToUser(SteamId t, std::span<const std::byte> d, int f, int c) override {
        sent.push_back({t, {d.begin(), d.end()}, f, c});
        return result;
    }
    std::vector<ReceivedMessage> ReceiveMessagesOnChannel(int c, int m) override {
        last_recv_channel = c;
        last_recv_max = m;
        return std::exchange(inbox, {});
    }
};

struct MockStats : ISteamUserStats {
    std::string found_name;
    int sort = -1, display = -1, method = -1;
    std::vector<SteamId> download_users;
    LeaderboardHandle download_handle = 0;
    std::vector<LeaderboardEntry> entries;
    std::vector<std::vector<std::int32_t>> uploads;
    void FindOrCreateLeaderboard(const std::string& n, int s, int d) override { found_name = n; sort = s; display = d; }
    void DownloadLeaderboardEntriesForUsers(LeaderboardHandle h, std::span<const SteamId> u) override {
        download_handle = h;
        download_users.assign(u.begin(), u.end());
    }
    bool GetDownloadedLeaderboardEntry(std::uint64_t, std::int32_t i, LeaderboardEntry& out) override {
        if (std::size_t(i) >= entries.size()) return false;
        out = entries[i];
        return true;
    }
    void UploadLeaderboardScore(LeaderboardHandle, int m, std::span<const std::int32_t> d) override {
        method = m;
        uploads.emplace_back(d.begin(), d.end());
    }
};

constexpr SteamId kMe = 100, kA = 201, kB = 202, kLobby = 900;

CoreState make_core() {
    CoreState c;
    c.own_id = kMe;
    c.channel = 3;
    c.max_len = 16;
    return c;
}

}  // namespace

// --- wire ----------------------------------------------------------------------------------------

TEST(Wire, RoundTrip) {
    const auto payload = bytes({1, 2, 3});
    const auto pkt = wire::encode({3, 7, 1, 3}, payload);
    ASSERT_EQ(pkt.size(), 10u);
    EXPECT_EQ(pkt[3], std::byte{3});  // little-endian len
    const auto h = wire::decode(pkt);
    ASSERT_TRUE(h);
    EXPECT_EQ(h->id, 3);
    EXPECT_EQ(h->channel, 7);
    EXPECT_EQ(h->flag, 1);
    EXPECT_EQ(h->len, 3u);
    EXPECT_TRUE(wire::validate(pkt, 16));
}

TEST(Wire, LengthIsLittleEndianUnaligned) {
    const auto pkt = wire::encode({1, 2, 3, 0x01020304}, {});
    EXPECT_EQ(pkt[3], std::byte{4});
    EXPECT_EQ(pkt[6], std::byte{1});
    EXPECT_EQ(wire::decode(pkt)->len, 0x01020304u);
}

TEST(Wire, InvalidHeaders) {
    EXPECT_FALSE(wire::decode(bytes({1, 2, 3, 0, 0, 0})));          // 6 bytes
    EXPECT_FALSE(wire::validate(bytes({1, 2, 3}), 16));
    EXPECT_FALSE(wire::validate(bytes({3, 0, 1, 5, 0, 0, 0, 9}), 16));   // len 5 != 1
    EXPECT_TRUE(wire::validate(bytes({3, 0, 1, 1, 0, 0, 0, 9}), 16));
    // flag 0 and len > max
    const auto big = wire::encode({3, 0, 0, 5}, bytes({1, 2, 3, 4, 5}));
    EXPECT_FALSE(wire::validate(big, 4));
    EXPECT_TRUE(wire::validate(big, 5));
    // flag != 0 ignores the maximum
    EXPECT_TRUE(wire::validate(wire::encode({3, 0, 1, 5}, bytes({1, 2, 3, 4, 5})), 4));
}

TEST(Wire, PollRuleDoesNotCheckLengthEquality) {
    EXPECT_FALSE(wire::poll_accepts(bytes({3, 0, 1, 0, 0, 0, 0}), 16));  // size 7: needs >= 8
    EXPECT_TRUE(wire::poll_accepts(bytes({3, 0, 1, 99, 0, 0, 0, 9}), 16));
    EXPECT_FALSE(wire::poll_accepts(bytes({3, 0, 0, 99, 0, 0, 0, 9}), 16));
}

// --- lobby ---------------------------------------------------------------------------------------

TEST(Lobby, Constants) {
    EXPECT_EQ(kMasterLobby.size(), 31u);
    EXPECT_EQ(kModVersion, "1.1.3");
    EXPECT_EQ(kLeaveMask, 0x10114u);
}

TEST(Lobby, CreatedOnlyWhenPending) {
    auto core = make_core();
    PeerList peers;
    MockMatchmaking mm;
    Lobby lobby(core, peers, mm);
    EXPECT_FALSE(lobby.OnLobbyCreated({1, kLobby}));
    EXPECT_EQ(mm.set_calls, 0);

    core.lobby_create_pending = true;
    EXPECT_TRUE(lobby.OnLobbyCreated({1, kLobby}));
    EXPECT_FALSE(core.lobby_create_pending);
    EXPECT_EQ(mm.data[std::make_pair(kLobby, std::string("nrsc_lobby_type"))], "nrsc_ynx3_seamless_master_lobby");
    EXPECT_EQ(mm.data[std::make_pair(kLobby, std::string("nrsc_lobby_version"))], "1.1.3");
    EXPECT_EQ(core.lobby_id, kLobby);
    EXPECT_EQ(core.owner_id, kMe);
    EXPECT_EQ(core.state_flags & kStateLobbyCreated, kStateLobbyCreated);
}

TEST(Lobby, CreatedStopsOnSetFailureButClearsPending) {
    auto core = make_core();
    core.lobby_create_pending = true;
    PeerList peers;
    MockMatchmaking mm;
    mm.fail_on_call = 2;
    Lobby lobby(core, peers, mm);
    EXPECT_FALSE(lobby.OnLobbyCreated({0, kLobby}));
    EXPECT_FALSE(core.lobby_create_pending);
    EXPECT_EQ(core.lobby_id, 0u);
    EXPECT_EQ(core.state_flags, 0u);
}

TEST(Lobby, EnterBuildsPeersAndMarksHost) {
    auto core = make_core();
    PeerList peers;
    MockMatchmaking mm;
    mm.data[{kLobby, "nrsc_lobby_type"}] = std::string(kMasterLobby);
    mm.members = {kMe, kA, kB};
    mm.owner = kB;
    Lobby lobby(core, peers, mm);
    EXPECT_TRUE(lobby.OnLobbyEnter({kLobby}));
    ASSERT_NE(peers.find_active(kA), nullptr);
    ASSERT_NE(peers.find_active(kB), nullptr);
    EXPECT_EQ(peers.find_active(kMe), nullptr);
    EXPECT_FALSE(peers.find_active(kA)->host());
    EXPECT_TRUE(peers.find_active(kB)->host());
    EXPECT_EQ(core.lobby_id, kLobby);
    EXPECT_EQ(core.owner_id, kB);
    EXPECT_EQ(core.state_flags & kStateLobbyEntered, kStateLobbyEntered);
}

TEST(Lobby, EnterDoesNotDuplicateExistingPeers) {
    auto core = make_core();
    PeerList peers;
    MockMatchmaking mm;
    mm.data[{kLobby, "nrsc_lobby_type"}] = std::string(kMasterLobby);
    mm.members = {kA};
    Lobby lobby(core, peers, mm);
    lobby.OnLobbyEnter({kLobby});
    lobby.OnLobbyEnter({kLobby});
    int active = 0;
    for (auto& p : peers.slots()) active += p.active();
    EXPECT_EQ(active, 1);
}

TEST(Lobby, EnterMismatchIgnored) {
    auto core = make_core();
    PeerList peers;
    MockMatchmaking mm;
    mm.members = {kA};
    Lobby lobby(core, peers, mm);
    EXPECT_FALSE(lobby.OnLobbyEnter({kLobby}));  // missing key
    mm.data[{kLobby, "nrsc_lobby_type"}] = "nrsc_ynx3_seamless_master_lobbx";
    EXPECT_FALSE(lobby.OnLobbyEnter({kLobby}));
    mm.data[{kLobby, "nrsc_lobby_type"}] = std::string(kMasterLobby) + "x";
    EXPECT_FALSE(lobby.OnLobbyEnter({kLobby}));
    EXPECT_EQ(peers.find_active(kA), nullptr);
    EXPECT_EQ(core.lobby_id, 0u);
    EXPECT_EQ(core.state_flags, 0u);
}

TEST(Lobby, EnterIgnoresLobbyVersion) {
    auto core = make_core();
    PeerList peers;
    MockMatchmaking mm;
    mm.data[{kLobby, "nrsc_lobby_type"}] = std::string(kMasterLobby);
    mm.data[{kLobby, "nrsc_lobby_version"}] = "9.9.9";
    Lobby lobby(core, peers, mm);
    EXPECT_TRUE(lobby.OnLobbyEnter({kLobby}));
}

TEST(Lobby, ChatUpdateLeaveStates) {
    for (std::uint32_t state : {kChatLeft, kChatDisconnected, kChatKicked, kChatBanned}) {
        auto core = make_core();
        core.lobby_id = kLobby;
        PeerList peers;
        PeerList::activate(peers.slot(0), kA);
        MockMatchmaking mm;
        std::vector<SteamId> left;
        Lobby lobby(core, peers, mm, [&](SteamId id) { left.push_back(id); });
        lobby.OnChatUpdate({kLobby, kA, state});
        EXPECT_EQ(peers.find_active(kA), nullptr) << state;
        EXPECT_EQ(left, (std::vector<SteamId>{kA})) << state;
    }
}

TEST(Lobby, ChatUpdateEnteredAndFilters) {
    auto core = make_core();
    core.lobby_id = kLobby;
    PeerList peers;
    MockMatchmaking mm;
    int leaves = 0;
    Lobby lobby(core, peers, mm, [&](SteamId) { ++leaves; });
    lobby.OnChatUpdate({kLobby, kA, kChatEntered});
    ASSERT_NE(peers.find_active(kA), nullptr);
    EXPECT_FALSE(peers.find_active(kA)->host());
    lobby.OnChatUpdate({kLobby, kA, kChatEntered});  // already present
    int active = 0;
    for (auto& p : peers.slots()) active += p.active();
    EXPECT_EQ(active, 1);
    lobby.OnChatUpdate({kLobby + 1, kA, kChatLeft});  // other lobby
    lobby.OnChatUpdate({kLobby, kMe, kChatLeft});     // ourselves
    lobby.OnChatUpdate({kLobby, kA, 0});              // unknown state
    lobby.OnChatUpdate({kLobby, kA, 3});              // not in mask
    lobby.OnChatUpdate({kLobby, kA, 0x20});           // out of range
    EXPECT_NE(peers.find_active(kA), nullptr);
    EXPECT_EQ(leaves, 0);
    lobby.OnChatUpdate({kLobby, kB, kChatLeft});  // unknown peer: no event
    EXPECT_EQ(leaves, 0);
}

// --- message bus ---------------------------------------------------------------------------------

struct BusFixture : ::testing::Test {
    CoreState core = make_core();
    PeerList peers;
    MockNet net;
    MessageBus bus{core, peers, net};
    struct Call { SteamId sender; std::size_t size; std::uint32_t remaining; };
    std::vector<Call> calls;

    void SetUp() override {
        PeerList::activate(peers.slot(0), kA);
        bus.register_handler(3, [this](Peer& p, std::span<const std::byte> pk, std::uint32_t rem) {
            calls.push_back({p.steam_id, pk.size(), rem});
        });
    }
    static ReceivedMessage msg(SteamId from, std::vector<std::byte> data, std::uint32_t type = kIdentityTypeSteamId) {
        return {type, from, std::move(data)};
    }
};

TEST_F(BusFixture, PollDispatchesWithRemaining) {
    net.inbox.push_back(msg(kA, wire::encode({3, 0, 0, 2}, bytes({7, 8}))));
    EXPECT_EQ(bus.poll(), 1);
    EXPECT_EQ(net.last_recv_channel, 3);
    EXPECT_EQ(net.last_recv_max, 256);
    ASSERT_EQ(calls.size(), 1u);
    EXPECT_EQ(calls[0].sender, kA);
    EXPECT_EQ(calls[0].size, 9u);
    EXPECT_EQ(calls[0].remaining, 2u);
}

TEST_F(BusFixture, PollDropsUnknownAndInactivePeers) {
    PeerList::activate(peers.slot(1), kB);
    peers.slot(1).flags = 0;  // inactive
    net.inbox.push_back(msg(kB, wire::encode({3, 0, 1, 1}, bytes({1}))));
    net.inbox.push_back(msg(999, wire::encode({3, 0, 1, 1}, bytes({1}))));
    net.inbox.push_back(msg(kA, wire::encode({3, 0, 1, 1}, bytes({1})), 1));  // wrong identity type
    EXPECT_EQ(bus.poll(), 0);
    EXPECT_TRUE(calls.empty());
}

TEST_F(BusFixture, PollDropsBadSizeLengthAndUnknownIds) {
    net.inbox.push_back(msg(kA, bytes({3, 0, 1, 0, 0, 0, 0})));                  // 7 bytes
    net.inbox.push_back(msg(kA, wire::encode({3, 0, 0, 17}, std::vector<std::byte>(17))));  // > max 16
    net.inbox.push_back(msg(kA, wire::encode({9, 0, 1, 1}, bytes({1}))));         // no handler 9
    EXPECT_EQ(bus.poll(), 0);
    net.inbox.push_back(msg(kA, wire::encode({3, 0, 1, 17}, std::vector<std::byte>(17))));  // flag != 0: ok
    EXPECT_EQ(bus.poll(), 1);
}

TEST_F(BusFixture, PollHandlesManyMessages) {
    for (int i = 0; i < 300; ++i) net.inbox.push_back(msg(kA, wire::encode({3, 0, 1, 1}, bytes({1}))));
    EXPECT_EQ(bus.poll(), 256);
}

TEST_F(BusFixture, EmptyHandlerThrowsBadFunctionCall) {
    bus.register_handler(4, nullptr);
    net.inbox.push_back(msg(kA, wire::encode({4, 0, 1, 1}, bytes({1}))));
    EXPECT_THROW(bus.poll(), std::bad_function_call);
}

TEST_F(BusFixture, SendFlagsAndPacket) {
    const auto payload = bytes({5, 6});
    EXPECT_TRUE(bus.send(kA, payload, 2));
    ASSERT_EQ(net.sent.size(), 1u);
    EXPECT_EQ(net.sent[0].target, kA);
    EXPECT_EQ(net.sent[0].flags, 9);
    EXPECT_EQ(net.sent[0].channel, 3);
    EXPECT_EQ(net.sent[0].data, wire::encode({3, 2, 0, 2}, payload));

    bus.unreliable_channels().push_back(2);
    bus.send(kA, payload, 2);
    EXPECT_EQ(net.sent[1].flags, 5);
    bus.send(kA, payload, 1);
    EXPECT_EQ(net.sent[2].flags, 9);
}

TEST_F(BusFixture, SendToUnknownPeerAndFailure) {
    EXPECT_FALSE(bus.send(kB, bytes({1}), 0));
    EXPECT_TRUE(net.sent.empty());
    net.result = 2;
    EXPECT_FALSE(bus.send(kA, bytes({1}), 0));
}

TEST_F(BusFixture, ControlMessage) {
    core.state_flags = 0x8;
    bus.send_control(peers.slot(0));
    ASSERT_EQ(net.sent.size(), 1u);
    EXPECT_EQ(net.sent[0].data, bytes({3, 1, 0, 1, 0, 0, 0, 1}));
    EXPECT_EQ(net.sent[0].flags, 9);
    core.state_flags = 0x7;
    bus.send_control(peers.slot(0));
    EXPECT_EQ(net.sent[1].data.back(), std::byte{0});
}

TEST(GameComponent, DispatchesAndMissingIsFatal) {
    GameComponentMap map;
    int seen = -1;
    map[5] = [&](int key, void*) { seen = key; };
    dispatch_game_component(map, 5, nullptr);
    EXPECT_EQ(seen, 5);
    try {
        dispatch_game_component(map, 6, nullptr);
        FAIL() << "expected FatalError";
    } catch (const FatalError& e) {
        EXPECT_STREQ(e.what(), "YuiNexusNetRankingDB");
        EXPECT_EQ(e.source_file(), "nrsc\\mod\\server_manager\\server_manager.cpp");
        EXPECT_EQ(e.line(), 0x9bu);
    }
}

// --- leaderboard ---------------------------------------------------------------------------------

namespace {
std::vector<Threshold> thr(std::initializer_list<std::uint32_t> v) {
    std::vector<Threshold> out;
    for (auto x : v) out.push_back({0, x});
    return out;
}
}  // namespace

TEST(Leaderboard, FindDownloadStateMachine) {
    auto core = make_core();
    MockStats stats;
    Leaderboard lb(core, stats);
    EXPECT_EQ(lb.state(), 0);
    lb.start_find();
    EXPECT_EQ(stats.found_name, "yui_nexus_3_seamless_deep_night_leaderboard_000");
    EXPECT_EQ(stats.sort, 2);
    EXPECT_EQ(stats.display, 1);
    EXPECT_EQ(lb.state(), 2);
    lb.on_find_result(77);
    lb.start_download();
    EXPECT_EQ(stats.download_handle, 77u);
    EXPECT_EQ(stats.download_users, (std::vector<SteamId>{kMe}));
    EXPECT_EQ(lb.state(), 4);

    stats.entries = {{kA, {9, 9}}, {kMe, {3, 450}}};
    lb.on_download_result(1, 2);
    EXPECT_EQ(lb.tier(), 3);
    EXPECT_EQ(lb.value(), 450u);
    lb.on_download_result(1, 0);
    EXPECT_EQ(lb.tier(), 0);
    EXPECT_EQ(lb.value(), 0u);
}

TEST(Leaderboard, DownloadWithoutOwnEntryKeepsValues) {
    auto core = make_core();
    MockStats stats;
    Leaderboard lb(core, stats);
    lb.set_tier(2);
    lb.set_value(10);
    stats.entries = {{kA, {9, 9}}};
    lb.on_download_result(1, 1);
    EXPECT_EQ(lb.tier(), 2);
    EXPECT_EQ(lb.value(), 10u);
}

TEST(Leaderboard, TierEdgeCases) {
    const auto t = thr({10, 20, 30});
    EXPECT_EQ(compute_tier(0, t).tier, 0);
    EXPECT_EQ(compute_tier(9, t).tier, 0);
    EXPECT_EQ(compute_tier(10, t).tier, 1);   // equal is not "greater": next threshold
    EXPECT_EQ(compute_tier(19, t).tier, 1);
    EXPECT_EQ(compute_tier(20, t).tier, 2);
    EXPECT_EQ(compute_tier(29, t).tier, 2);
    EXPECT_EQ(compute_tier(30, t).tier, 3);
    EXPECT_EQ(compute_tier(0xffffffffu, t).tier, 3);  // unsigned compare
    EXPECT_EQ(compute_tier(5, {}).tier, 0);
    EXPECT_EQ(compute_tier(5, {}).value, 0u);
}

TEST(Leaderboard, TierRepresentativeValue) {
    const auto t = thr({10, 20, 30});
    EXPECT_EQ(compute_tier(5, t).value, 0u);
    EXPECT_EQ(compute_tier(15, t).value, 5u);    // 10 / 2
    EXPECT_EQ(compute_tier(25, t).value, 15u);   // (20 + 10) / 2
    EXPECT_EQ(compute_tier(99, t).value, 25u);   // (30 + 20) / 2
    EXPECT_EQ(compute_tier(99, thr({7})).value, 3u);
    const auto w = thr({0x80000000u, 0x80000000u});
    EXPECT_EQ(compute_tier(0xffffffffu, w).value, 0u);  // 32-bit wrap before the shift
}

TEST(Leaderboard, UploadStepAddPath) {
    auto core = make_core();
    MockStats stats;
    Leaderboard lb(core, stats);
    lb.on_find_result(5);
    EXPECT_FALSE(lb.upload_step());
    lb.set_tier(2);
    lb.set_value(100);
    lb.add_to_accumulator(25);
    core.leaderboard_add_pending = true;
    EXPECT_TRUE(lb.upload_step());
    EXPECT_EQ(stats.method, 2);
    ASSERT_EQ(stats.uploads.size(), 1u);
    EXPECT_EQ(stats.uploads[0], (std::vector<std::int32_t>{2, 125}));
    EXPECT_FALSE(core.leaderboard_add_pending);
    EXPECT_EQ(lb.accumulator(), 0u);
    EXPECT_FALSE(lb.upload_step());
}

TEST(Leaderboard, UploadStepRefreshPath) {
    auto core = make_core();
    MockStats stats;
    Leaderboard lb(core, stats);
    lb.thresholds() = thr({10, 20, 30});
    lb.set_value(25);
    core.leaderboard_refresh_pending = true;
    EXPECT_TRUE(lb.upload_step());
    EXPECT_EQ(stats.uploads.back(), (std::vector<std::int32_t>{2, 15}));
    EXPECT_EQ(lb.tier(), 2);
    EXPECT_EQ(lb.value(), 15u);
    EXPECT_FALSE(core.leaderboard_refresh_pending);

    // Recompute giving the same value: no upload.
    lb.thresholds() = thr({10, 20, 30});
    lb.set_value(0);
    core.leaderboard_refresh_pending = true;
    EXPECT_FALSE(lb.upload_step());
    EXPECT_EQ(stats.uploads.size(), 1u);
    EXPECT_FALSE(core.leaderboard_refresh_pending);
}

TEST(Leaderboard, UploadStepAddThenRefresh) {
    auto core = make_core();
    MockStats stats;
    Leaderboard lb(core, stats);
    lb.thresholds() = thr({10, 20, 30});
    lb.set_value(5);
    lb.add_to_accumulator(12);
    core.leaderboard_add_pending = true;
    core.leaderboard_refresh_pending = true;
    EXPECT_TRUE(lb.upload_step());
    ASSERT_EQ(stats.uploads.size(), 1u);  // single upload, from the refresh
    EXPECT_EQ(stats.uploads[0], (std::vector<std::int32_t>{1, 5}));  // 17 -> tier 1 -> 10/2
    EXPECT_FALSE(core.leaderboard_add_pending);
    EXPECT_FALSE(core.leaderboard_refresh_pending);
}

// --- task timer (partial 0x180061140) ------------------------------------------------------------

TEST(TaskTimer, ResetRule) {
    TaskTimers t;
    t.t14c = 0.7f;
    accumulate_timer(t, 0.5f);
    EXPECT_FLOAT_EQ(t.t148, 0.5f);
    accumulate_timer(t, 0.5f);  // reaches 1.0 with t14c >= 2/3
    EXPECT_FLOAT_EQ(t.t148, 0.0f);
}

TEST(TaskTimer, NoResetBelowEitherThreshold) {
    TaskTimers t;
    t.t14c = 0.6f;  // < 2/3
    accumulate_timer(t, 1.5f);
    EXPECT_FLOAT_EQ(t.t148, 1.5f);
    TaskTimers u;
    u.t14c = 1.0f;
    accumulate_timer(u, 0.99f);  // < 1.0
    EXPECT_FLOAT_EQ(u.t148, 0.99f);
    TaskTimers v;
    v.t14c = kResetGate;  // boundary is inclusive
    accumulate_timer(v, 1.0f);
    EXPECT_FLOAT_EQ(v.t148, 0.0f);
}

TEST(TaskTimer, MarksOnlyPeersWithFlagExactlyOne) {
    PeerList peers(4);
    peers.slot(0).flags = 1;
    peers.slot(1).flags = 2;
    peers.slot(2).flags = 0;
    peers.slot(3).flags = kPeerActive | kPeerHost;
    int sent = 0;
    EXPECT_EQ(mark_pending_peers(peers, [&](Peer&) { ++sent; }), 1);
    EXPECT_EQ(sent, 1);
    EXPECT_EQ(peers.slot(0).flags, 0x11u);
    EXPECT_EQ(peers.slot(0).field3c, 1u);
    EXPECT_EQ(peers.slot(1).flags, 2u);
    EXPECT_EQ(peers.slot(2).flags, 0u);
    EXPECT_EQ(peers.slot(3).flags, 0x21u);
    EXPECT_EQ(mark_pending_peers(peers, nullptr), 0);  // already 0x11: not marked again
}
