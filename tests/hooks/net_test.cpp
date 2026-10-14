// Net group (WP10): detours on fake game objects with a mocked Steam networking interface, with and without an
// attached session layer (docs/hooks.md section 9; evidence/hooks/detours/group1..4.md).
#include <gtest/gtest.h>

#include <cstring>
#include <map>

#include "nrsc/hooks/net/net_group.hpp"
#include "nrsc/server/message_bus.hpp"

using namespace nrsc::hooks;
using namespace nrsc::hooks::net;
using namespace nrsc::server;

namespace {

struct SentMsg {
    SteamId target;
    std::vector<std::byte> data;
    int flags, channel;
};

// Same shape as the mock in tests/server/server_test.cpp.
struct MockNet : ISteamNetworkingMessages {
    std::vector<SentMsg> sent;
    int SendMessageToUser(SteamId t, std::span<const std::byte> d, int f, int c) override {
        sent.push_back({t, {d.begin(), d.end()}, f, c});
        return kResultOk;
    }
    std::vector<ReceivedMessage> ReceiveMessagesOnChannel(int, int) override { return {}; }
};

constexpr SteamId kA = 0x1100000000000001ull, kB = 0x1100000000000002ull, kC = 0x1100000000000003ull;

// Payload of a bus packet: [3][channel][0][u32 len][payload].
std::vector<std::uint8_t> payload_of(const SentMsg& m) {
    std::vector<std::uint8_t> out;
    for (std::size_t i = 7; i < m.data.size(); ++i) out.push_back(std::uint8_t(m.data[i]));
    return out;
}
std::uint8_t channel_of(const SentMsg& m) { return std::uint8_t(m.data.at(1)); }

template <typename T>
T rd(const void* base, std::size_t off) {
    T v;
    std::memcpy(&v, static_cast<const std::uint8_t*>(base) + off, sizeof v);
    return v;
}
template <typename T>
void wr(void* base, std::size_t off, T v) {
    std::memcpy(static_cast<std::uint8_t*>(base) + off, &v, sizeof v);
}

struct NetTest : ::testing::Test {
    CoreState core;
    PeerList peers{8};
    MockNet net;
    MessageBus bus{core, peers, net};
    NetState* s = nullptr;

    void SetUp() override {
        core.channel = 3;
        s = &reset_net_state();
        PeerList::activate(peers.slot(0), kA);
        PeerList::activate(peers.slot(1), kB);
        PeerList::activate(peers.slot(2), kC);
        peers.slot(2).flags = 0;  // inactive third slot (id set, active bit clear)
    }
    void attach() { s->attach(bus, peers); }
};

// ---------------------------------------------------------------- hook table

TEST_F(NetTest, SpecsUseTheSignatureSlots) {
    std::map<unsigned, std::uint64_t> table;
    std::uint64_t v = 0x140100000;
    for (unsigned off : {kSlotSetClassA, kSlotSetClassB, kSlotBossListCopy, kSlotRateSnoop, kSlotGameFlagGetter, kSlotDecreaseJob,
                         kSlotUpdateJob, kSlotDebugSession, kSlotNetworkSession, kSlotTeamCollect, kSlotTeamSend,
                         kSlotMapItemRequest, kSlotSessionBroadcast})
        table[off] = v += 0x1000;
    const auto specs = build_specs([&](unsigned off) { return table.at(off); }, *s);
    ASSERT_EQ(specs.size(), 13u);
    auto find = [&](const std::string& name) -> const HookSpec& {
        for (const auto& h : specs)
            if (h.name.find(name) == 0) return h;
        ADD_FAILURE() << name;
        return specs[0];
    };
    EXPECT_EQ(find("hook#8").site, table[0x138]);
    EXPECT_EQ(find("hook#8").kind, Kind::CallSite);
    EXPECT_EQ(find("hook#8").expect, std::vector<std::uint8_t>{0xE8});
    EXPECT_EQ(find("hook#8").detour, find("hook#9").detour);       // one detour, two sites
    EXPECT_EQ(find("hook#8").original, find("hook#9").original);   // both call 0x140202aa0
    EXPECT_EQ(find("hook#12").kind, Kind::Inline);                  // deviation from E9-at-entry, see header
    EXPECT_EQ(find("hook#14").site, table[0x168]);
    EXPECT_EQ(find("hook#18").kind, Kind::VtableSlot);
    EXPECT_EQ(find("hook#18").site, table[0x238] + 0x8);
    EXPECT_EQ(find("hook#19").site, table[0x240] + 0x18);
    EXPECT_EQ(find("hookP2").kind, Kind::Inline);
    EXPECT_EQ(find("hookP4").kind, Kind::Inline);
    EXPECT_EQ(find("hookP9").site, table[0x2c8]);
    for (const auto& h : specs) {
        EXPECT_NE(h.detour, nullptr) << h.name;
        EXPECT_NE(h.original, nullptr) << h.name;  // every hook has a way back to the game
    }
}

TEST_F(NetTest, UnresolvedVtableStaysZero) {
    const auto specs = build_specs([](unsigned) { return std::uint64_t{0}; }, *s);
    for (const auto& h : specs)
        if (h.kind == Kind::VtableSlot) { EXPECT_EQ(h.site, 0u); }
}

// ---------------------------------------------------------------- #8 / #9

int g_set_class_calls = 0;
void fake_set_class(void*, void*, std::uint32_t) { ++g_set_class_calls; }

TEST_F(NetTest, ClassIdSentToActivePeersWhenLocal) {
    g_set_class_calls = 0;
    s->original_set_class = reinterpret_cast<void*>(&fake_set_class);
    attach();
    std::uint8_t pgd_local[16] = {}, pgd_other[16] = {}, gdm[16] = {};
    wr<void*>(gdm, 8, pgd_local);
    detour_set_class(gdm, pgd_local, 0x01020304);
    EXPECT_EQ(g_set_class_calls, 1);  // original first
    ASSERT_EQ(net.sent.size(), 2u);   // peers A and B; C is inactive
    for (const auto& m : net.sent) {
        EXPECT_EQ(channel_of(m), 7);
        EXPECT_EQ(payload_of(m), (std::vector<std::uint8_t>{0x04, 0x03, 0x02, 0x01}));
    }
    EXPECT_EQ(net.sent[0].target, kA);
    EXPECT_EQ(net.sent[1].target, kB);
    net.sent.clear();
    detour_set_class(gdm, pgd_other, 5);  // not the local PlayerGameData
    EXPECT_EQ(g_set_class_calls, 2);
    EXPECT_TRUE(net.sent.empty());
}

TEST_F(NetTest, ClassIdWithoutSessionOnlyCallsTheOriginal) {
    g_set_class_calls = 0;
    s->original_set_class = reinterpret_cast<void*>(&fake_set_class);
    std::uint8_t pgd[16] = {}, gdm[16] = {};
    wr<void*>(gdm, 8, pgd);
    detour_set_class(gdm, pgd, 9);
    EXPECT_EQ(g_set_class_calls, 1);
    EXPECT_TRUE(net.sent.empty());
}

// ---------------------------------------------------------------- #12

void* fake_alloc(GameAllocator*, std::size_t size, std::size_t align) {
    EXPECT_EQ(align, 1u);
    return new std::uint8_t[size];
}
const GameAllocatorVtbl kFakeAllocVtbl{{}, &fake_alloc};
GameAllocator g_alloc{&kFakeAllocVtbl};

ByteBlob* fake_blob_copy(ByteBlob* out, ByteBlob* src) {
    *out = *src;
    out->u24 = 0xdead;  // distinguishable
    return out;
}

TEST_F(NetTest, BossListReplacedWithSessionAndList) {
    s->original_boss_list = reinterpret_cast<void*>(&fake_blob_copy);
    s->boss_list = BossListSource{{3, 1, 4, 1, 5}, 0x77};
    ByteBlob src{&g_alloc, nullptr, nullptr, nullptr, 0x11, 0x22}, out{};
    // no session: original
    EXPECT_EQ(detour_boss_list(&out, &src), &out);
    EXPECT_EQ(out.u24, 0xdeadu);
    attach();
    out = {};
    EXPECT_EQ(detour_boss_list(&out, &src), &out);
    ASSERT_NE(out.begin, nullptr);
    EXPECT_EQ(out.end - out.begin, 5);
    EXPECT_EQ(out.cap, out.end);
    EXPECT_EQ(out.begin[2], 4);
    EXPECT_EQ(out.alloc, &g_alloc);
    EXPECT_EQ(out.u20, 0x77u);
    EXPECT_EQ(src.u20, 0x77u);  // the source is overwritten too
    EXPECT_EQ(out.u24, 0x22u);
    delete[] out.begin;
    // session but no list configured (U default): original
    s->boss_list.reset();
    out = {};
    detour_boss_list(&out, &src);
    EXPECT_EQ(out.u24, 0xdeadu);
}

TEST_F(NetTest, EmptyBossListGivesEmptyBlob) {
    s->boss_list = BossListSource{{}, 2};
    attach();
    ByteBlob src{&g_alloc, nullptr, nullptr, nullptr, 0, 9}, out{};
    detour_boss_list(&out, &src);
    EXPECT_EQ(out.begin, nullptr);
    EXPECT_EQ(out.u20, 2u);
}

// ---------------------------------------------------------------- #13

int g_snoop_calls = 0;
std::uint32_t g_snoop_g = 0;
void fake_snoop(void*, void*, void*, bool, std::uint32_t g, std::uint8_t, std::uint32_t) {
    ++g_snoop_calls;
    g_snoop_g = g;
}

TEST_F(NetTest, RateSelectorSnoopedThenOriginal) {
    g_snoop_calls = 0;
    s->original_rate_snoop = reinterpret_cast<void*>(&fake_snoop);
    std::uint8_t entry[16] = {};
    entry[4] = 1;
    detour_rate_snoop(nullptr, nullptr, entry, true, 42, 0, 0);
    EXPECT_EQ(s->rate.selector, 1);
    EXPECT_EQ(g_snoop_calls, 1);
    EXPECT_EQ(g_snoop_g, 42u);
    entry[4] = 0;
    detour_rate_snoop(nullptr, nullptr, entry, true, 7, 0, 0);
    EXPECT_EQ(s->rate.selector, 0);
}

// ---------------------------------------------------------------- #14

bool fake_getter() { return true; }

TEST_F(NetTest, GameFlagGetter) {
    s->original_game_flag = reinterpret_cast<void*>(&fake_getter);
    EXPECT_TRUE(detour_game_flag());  // no session: original
    attach();
    EXPECT_FALSE(detour_game_flag());  // U defaults: no force byte, no singleton
    std::uint8_t gdm[0x100] = {};
    void* slot = gdm;
    s->game_data_slot = &slot;
    EXPECT_FALSE(detour_game_flag());
    gdm[0xd0] = 1;
    EXPECT_TRUE(detour_game_flag());  // singleton byte +0xd0 (the game reads +0xd8)
    gdm[0xd0] = 0;
    std::uint8_t force = 1;
    s->force_flag = &force;
    EXPECT_TRUE(detour_game_flag());
    force = 0;
    EXPECT_FALSE(detour_game_flag());
}

// ---------------------------------------------------------------- #15 / #16

struct JobFixture : NetTest {
    std::vector<void*> allocs;
    std::vector<LocalJobCallback*> wrapped;
    std::vector<std::pair<void*, void*>> attached_pairs;
    std::uint8_t gdm[0x200] = {};

    static JobFixture*& self() { static JobFixture* p; return p; }
    void SetUp() override {
        NetTest::SetUp();
        self() = this;
        auto& h = s->jobs;
        h.alloc_object = [](std::size_t size, std::size_t align) -> void* {
            EXPECT_EQ(align, 8u);
            void* p = ::operator new(size);
            self()->allocs.push_back(p);
            return p;
        };
        h.alloc_data = [](std::size_t size) -> void* { return ::operator new(size); };
        h.wrap_callback = [](void** scratch, void* cb) -> void* {
            EXPECT_EQ(*scratch, nullptr);
            self()->wrapped.push_back(static_cast<LocalJobCallback*>(cb));
            return cb;
        };
        h.attach_callback = [](void* job, void* w) { self()->attached_pairs.push_back({job, w}); };
        h.decrease_vtbl = reinterpret_cast<const void*>(0xd1);
        h.update_vtbl = reinterpret_cast<const void*>(0xe1);
        h.game_data = gdm;
        s->rate.game_data = gdm;
    }
};

void** fake_job_factory(void*, void** out) {
    *out = reinterpret_cast<void*>(0xf00d);
    return out;
}

TEST_F(JobFixture, DecreaseJobIsLocalAndFlagsDone) {
    s->original_decrease_job = reinterpret_cast<void*>(&fake_job_factory);
    void* out = nullptr;
    detour_decrease_job(nullptr, &out);  // no session: the game's factory
    EXPECT_EQ(out, reinterpret_cast<void*>(0xf00d));
    attach();
    out = nullptr;
    EXPECT_EQ(detour_decrease_job(nullptr, &out), &out);
    ASSERT_NE(out, nullptr);
    ASSERT_EQ(allocs.size(), 1u);
    EXPECT_EQ(out, allocs[0]);
    EXPECT_EQ(rd<const void*>(out, 0), reinterpret_cast<const void*>(0xd1));
    EXPECT_EQ(rd<std::uint64_t>(out, 8), 1u);  // refcount
    EXPECT_EQ(rd<std::uint64_t>(out, 0x70), 0u);
    ASSERT_EQ(wrapped.size(), 2u);  // two callbacks attached
    EXPECT_EQ(attached_pairs.size(), 2u);
    EXPECT_EQ(wrapped[0]->job, out);
    EXPECT_EQ(wrapped[0]->self, wrapped[0]);
    EXPECT_EQ(gdm[0x15a], 0);
    wrapped[0]->vtbl->do_call(wrapped[0]);  // the "server result": raises +0x15a, nothing else
    EXPECT_EQ(gdm[0x15a], 1);
    EXPECT_EQ(gdm[0x159], 0);
    ::operator delete(out);
}

TEST_F(JobFixture, DecreaseJobFallsBackWhenHostIncomplete) {
    s->original_decrease_job = reinterpret_cast<void*>(&fake_job_factory);
    attach();
    s->jobs.attach_callback = nullptr;  // U member missing
    void* out = nullptr;
    detour_decrease_job(nullptr, &out);
    EXPECT_EQ(out, reinterpret_cast<void*>(0xf00d));
    EXPECT_TRUE(allocs.empty());
}

TEST_F(JobFixture, UpdateJobCopiesArguments) {
    attach();
    std::uint64_t v64d[] = {1, 2, 3};
    std::uint32_t v32d[] = {10, 20};
    std::uint8_t v8d[] = {7, 8, 9, 10, 11};
    GameVec v64{v64d, v64d + 3, v64d + 3}, v32{v32d, v32d + 2, v32d + 2}, v8{v8d, v8d + 5, v8d + 5};
    void* out = nullptr;
    detour_update_job(nullptr, &out, 0xaaaa, 0xbbbb, &v64, &v32, 5, 6, &v8, 1);
    ASSERT_NE(out, nullptr);
    EXPECT_EQ(rd<const void*>(out, 0), reinterpret_cast<const void*>(0xe1));
    EXPECT_EQ(rd<std::uint64_t>(out, 0x78), 0xaaaau);
    EXPECT_EQ(rd<std::uint64_t>(out, 0x80), 0xbbbbu);
    EXPECT_EQ(rd<std::uint8_t*>(out, 0x68), static_cast<std::uint8_t*>(out) + 0x70);
    auto* p64 = rd<std::uint64_t*>(out, 0x88);
    EXPECT_EQ(rd<std::uint64_t*>(out, 0x90) - p64, 3);
    EXPECT_EQ(p64[2], 3u);
    auto* p32 = rd<std::uint32_t*>(out, 0xa0);
    EXPECT_EQ(rd<std::uint32_t*>(out, 0xa8) - p32, 2);
    EXPECT_EQ(p32[1], 20u);
    auto* p8 = rd<std::uint8_t*>(out, 0xc0);
    EXPECT_EQ(rd<std::uint8_t*>(out, 0xc8) - p8, 5);
    EXPECT_EQ(p8[4], 11);
    EXPECT_EQ(rd<std::uint32_t>(out, 0xb8), 5u);
    EXPECT_EQ(rd<std::uint32_t>(out, 0xbc), 6u);
    EXPECT_EQ(rd<std::uint8_t>(out, 0xd8), 1);
    EXPECT_EQ(rd<std::uint64_t>(out, 8), 1u);
    ASSERT_EQ(wrapped.size(), 2u);
    EXPECT_EQ(wrapped[1]->arg0, &s->rate);
    EXPECT_EQ(wrapped[1]->arg1, gdm);
    ::operator delete(p64); ::operator delete(p32); ::operator delete(p8); ::operator delete(out);
}

TEST_F(JobFixture, UpdateJobFallsBackWithoutSession) {
    s->original_update_job = reinterpret_cast<void*>(+[](void*, void** out, std::uint64_t, std::uint64_t, const GameVec*,
                                                         const GameVec*, std::uint32_t, std::uint32_t, const GameVec*,
                                                         std::uint8_t) -> void** {
        *out = reinterpret_cast<void*>(0xbeef);
        return out;
    });
    void* out = nullptr;
    detour_update_job(nullptr, &out, 0, 0, nullptr, nullptr, 0, 0, nullptr, 0);
    EXPECT_EQ(out, reinterpret_cast<void*>(0xbeef));
}

TEST_F(JobFixture, RateCallbackRanges) {
    attach();
    LocalJobCallback cb{};
    cb.arg0 = &s->rate;
    int r = 0;
    s->random = [&] { return r; };
    // gate bit clear: nothing happens
    gdm[0] = 0;
    r = 10;
    rate_update_callback(&cb);
    EXPECT_EQ(gdm[0x159], 0);
    gdm[0] = 0x40;
    // selector bit 0 set: 200 + r % 300
    s->rate.selector = 1;
    r = 1234;
    rate_update_callback(&cb);
    EXPECT_EQ(s->rate.delta, 1234 % 300 + 200);
    EXPECT_EQ(gdm[0x159], 1);
    // selector clear: m = r % 150; cur <= m + 100 -> -cur, else -100 - m
    gdm[0x159] = 0;
    s->rate.selector = 0;
    s->rate.current_rate = 50;
    r = 40;  // m = 40, 50 <= 140 -> -50
    rate_update_callback(&cb);
    EXPECT_EQ(s->rate.delta, -50);
    s->rate.current_rate = 500;
    r = 40;  // 500 > 140 -> -140
    rate_update_callback(&cb);
    EXPECT_EQ(s->rate.delta, -140);
    EXPECT_EQ(gdm[0x159], 1);
}

TEST_F(JobFixture, NewJobReplacesOldCallbacks) {
    attach();
    void *a = nullptr, *b = nullptr;
    detour_decrease_job(nullptr, &a);
    LocalJobCallback* first = s->jobs.decrease_cb[0].get();
    detour_decrease_job(nullptr, &b);
    EXPECT_NE(s->jobs.decrease_cb[0].get(), nullptr);
    EXPECT_EQ(s->jobs.decrease_cb[0]->job, b);
    (void)first;
    ::operator delete(a);
    ::operator delete(b);
}

// ---------------------------------------------------------------- #18 / #19

bool g_dbg_orig = false;
bool fake_debug_send(void*, void*, std::uint32_t, std::uint32_t type) {
    g_dbg_orig = true;
    return type == 99;
}

TEST_F(NetTest, DebugSessionSendsTypes1And18ToPeers) {
    s->original_debug_send = reinterpret_cast<void*>(&fake_debug_send);
    attach();
    const std::uint8_t data[3] = {9, 8, 7};
    g_dbg_orig = false;
    EXPECT_TRUE(detour_debug_send(nullptr, const_cast<std::uint8_t*>(data), 3, 1));
    EXPECT_FALSE(g_dbg_orig);
    ASSERT_EQ(net.sent.size(), 2u);
    EXPECT_EQ(channel_of(net.sent[0]), 5);
    EXPECT_EQ(payload_of(net.sent[0]), (std::vector<std::uint8_t>{9, 8, 7}));
    net.sent.clear();
    EXPECT_TRUE(detour_debug_send(nullptr, const_cast<std::uint8_t*>(data), 3, 0x18));
    ASSERT_EQ(net.sent.size(), 2u);
    EXPECT_EQ(channel_of(net.sent[1]), 6);
    net.sent.clear();
    EXPECT_TRUE(detour_debug_send(nullptr, const_cast<std::uint8_t*>(data), 3, 99));  // other type: original
    EXPECT_TRUE(g_dbg_orig);
    EXPECT_TRUE(net.sent.empty());
}

TEST_F(NetTest, DebugSessionWithoutSessionCallsOriginal) {
    s->original_debug_send = reinterpret_cast<void*>(&fake_debug_send);
    g_dbg_orig = false;
    std::uint8_t data[1] = {};
    detour_debug_send(nullptr, data, 1, 1);
    EXPECT_TRUE(g_dbg_orig);
    EXPECT_TRUE(net.sent.empty());
}

int g_recv_orig = 0;
void* fake_recv(void*, void* out, void*, std::uint32_t) {
    ++g_recv_orig;
    return out;
}

TEST_F(NetTest, NetworkSessionReceivesFromPeerQueue) {
    s->original_network_recv = reinterpret_cast<void*>(&fake_recv);
    attach();
    std::uint8_t session[16] = {};
    wr<SteamId>(session, 8, kA);
    const std::byte msg[4] = {std::byte{1}, std::byte{2}, std::byte{3}, std::byte{4}};
    s->enqueue_received(kA, 5, msg);
    std::uint8_t buf[64] = {};
    std::uint8_t in[16] = {}, out[24] = {};
    wr<std::uint8_t*>(in, 0, buf);
    wr<std::uint64_t>(in, 8, 64);
    g_recv_orig = 0;
    EXPECT_EQ(detour_network_recv(session, out, in, 1), out);
    EXPECT_EQ(g_recv_orig, 0);
    EXPECT_EQ(rd<std::uint8_t*>(out, 0), buf);
    EXPECT_EQ(rd<std::uint64_t>(out, 8), 64u);
    EXPECT_EQ(rd<std::uint32_t>(out, 0x10), 4u);
    EXPECT_EQ(buf[3], 4);
    // queue empty now: original
    detour_network_recv(session, out, in, 1);
    EXPECT_EQ(g_recv_orig, 1);
    // channel 6 is separate from channel 5
    s->enqueue_received(kA, 5, msg);
    detour_network_recv(session, out, in, 0x18);
    EXPECT_EQ(g_recv_orig, 2);
    EXPECT_EQ(s->queued(kA, 5), 1u);
    // other type: original
    detour_network_recv(session, out, in, 7);
    EXPECT_EQ(g_recv_orig, 3);
}

TEST_F(NetTest, NetworkSessionLimitsToTheGameBuffer) {
    attach();
    std::uint8_t session[16] = {};
    wr<SteamId>(session, 8, kB);
    std::vector<std::byte> big(0x500, std::byte{0x5a});
    s->enqueue_received(kB, 6, big);
    std::uint8_t buf[16], guard[16];
    std::memset(buf, 0, sizeof buf);
    std::memset(guard, 0xcc, sizeof guard);
    std::uint8_t area[32];
    std::memcpy(area, buf, 16);
    std::memcpy(area + 16, guard, 16);
    std::uint8_t in[16] = {}, out[24] = {};
    wr<std::uint8_t*>(in, 0, area);
    wr<std::uint64_t>(in, 8, 16);
    detour_network_recv(session, out, in, 0x18);
    EXPECT_EQ(rd<std::uint32_t>(out, 0x10), 16u);
    EXPECT_EQ(area[15], 0x5a);
    EXPECT_EQ(area[16], 0xcc);  // not overrun
}

TEST_F(NetTest, NetworkSessionIgnoresInactivePeer) {
    attach();
    std::uint8_t session[16] = {};
    wr<SteamId>(session, 8, kC);  // slot 2 is inactive
    const std::byte msg[1] = {std::byte{1}};
    s->enqueue_received(kC, 5, msg);
    std::uint8_t buf[8] = {}, in[16] = {}, out[24] = {};
    wr<std::uint8_t*>(in, 0, buf);
    wr<std::uint64_t>(in, 8, 8);
    EXPECT_EQ(s->pop_received(kC, 5, buf, 8), 0u);
    detour_network_recv(session, out, in, 1);  // original is null: returns out untouched
    EXPECT_EQ(rd<std::uint32_t>(out, 0x10), 0u);
}

TEST_F(NetTest, NetworkSessionWithoutSessionCallsOriginal) {
    s->original_network_recv = reinterpret_cast<void*>(&fake_recv);
    g_recv_orig = 0;
    s->enqueue_received(kA, 5, std::span<const std::byte>{});
    std::uint8_t session[16] = {}, in[16] = {}, out[24] = {};
    detour_network_recv(session, out, in, 1);
    EXPECT_EQ(g_recv_orig, 1);
}

// ---------------------------------------------------------------- P1

bool fake_collect(void*, void*) { return true; }

TEST_F(NetTest, TeamCollectIsPassThrough) {
    EXPECT_FALSE(detour_team_collect(nullptr, nullptr));  // no original captured yet
    s->original_team_collect = reinterpret_cast<void*>(&fake_collect);
    EXPECT_TRUE(detour_team_collect(nullptr, nullptr));
    attach();
    EXPECT_TRUE(detour_team_collect(nullptr, nullptr));
}

// ---------------------------------------------------------------- P2

struct SendRecord {
    void* team;
    void* record;
    std::uint32_t type, len;
    std::vector<std::uint8_t> data;
};
std::vector<SendRecord> g_team_sends;
void fake_team_send(void* team, void* rec, std::uint32_t type, const void* data, std::uint32_t len) {
    const auto* p = static_cast<const std::uint8_t*>(data);
    g_team_sends.push_back({team, rec, type, len, {p, p + len}});
}
int g_team_orig = 0;
void fake_team_orig(void*, std::uint8_t, std::uint8_t, const GameVec*) { ++g_team_orig; }

TEST_F(NetTest, TeamSendReachesEveryPendingPeer) {
    g_team_sends.clear();
    g_team_orig = 0;
    s->original_team_send = reinterpret_cast<void*>(&fake_team_orig);
    s->team_send = &fake_team_send;
    std::uint8_t recs[4 * 0xf0] = {};
    recs[0xf0 + 0xe1] = 1;  // blocked
    recs[2 * 0xf0 + 0xe3] = 1;  // already sent
    std::uint8_t team[0x40] = {};
    wr<std::uint8_t*>(team, 0x30, recs);
    wr<std::uint8_t*>(team, 0x38, recs + sizeof recs);
    const std::uint8_t pd[3] = {1, 2, 3};
    GameVec payload{const_cast<std::uint8_t*>(pd), const_cast<std::uint8_t*>(pd) + 3, const_cast<std::uint8_t*>(pd) + 3};
    detour_team_send(team, 1, 1, &payload);  // no session: original, no sends
    EXPECT_EQ(g_team_orig, 1);
    EXPECT_TRUE(g_team_sends.empty());
    attach();
    detour_team_send(team, 1, 1, &payload);  // range [1,1] is ignored
    EXPECT_EQ(g_team_orig, 1);
    ASSERT_EQ(g_team_sends.size(), 2u);
    EXPECT_EQ(g_team_sends[0].record, recs);
    EXPECT_EQ(g_team_sends[1].record, recs + 3 * 0xf0);
    EXPECT_EQ(g_team_sends[0].type, 0x94u);
    EXPECT_EQ(g_team_sends[0].data, (std::vector<std::uint8_t>{1, 2, 3}));
    EXPECT_EQ(recs[0xe3], 1);
    EXPECT_EQ(recs[3 * 0xf0 + 0xe3], 1);
    EXPECT_EQ(recs[0xf0 + 0xe3], 0);  // blocked records are not marked
    detour_team_send(team, 0, 5, &payload);  // second call: everyone is done
    EXPECT_EQ(g_team_sends.size(), 2u);
}

TEST_F(NetTest, TeamSendWithoutSendFunctionCallsOriginal) {
    g_team_orig = 0;
    s->original_team_send = reinterpret_cast<void*>(&fake_team_orig);
    attach();
    std::uint8_t team[0x40] = {};
    GameVec payload{};
    detour_team_send(team, 0, 0, &payload);
    EXPECT_EQ(g_team_orig, 1);
}

// ---------------------------------------------------------------- P4

struct MapCall {
    std::uint64_t a1, a6;
    std::int64_t current;
    bool locked;
};
std::vector<MapCall> g_map_calls;
std::uint32_t fake_map_request(void*, std::uint64_t a1, std::uint64_t, const void*, void*, std::uint8_t, std::uint64_t a6) {
    NetState& st = net_state();
    g_map_calls.push_back({a1, a6, st.current_request.load(), !st.lock.try_lock()});
    if (g_map_calls.back().locked == false) st.lock.unlock();
    return 77;
}

TEST_F(NetTest, MapItemRequestForwardedInMode1) {
    g_map_calls.clear();
    s->original_map_item_request = reinterpret_cast<void*>(&fake_map_request);
    std::uint32_t mode = 1;
    s->mode_word = &mode;
    std::uint8_t rec[24];
    for (int i = 0; i < 24; ++i) rec[i] = std::uint8_t(0x30 + i);
    // no session: original even in mode 1
    EXPECT_EQ(detour_map_item_request(nullptr, 5, 0, rec, nullptr, 0, 6), 77u);
    ASSERT_EQ(g_map_calls.size(), 1u);
    EXPECT_FALSE(g_map_calls[0].locked);  // unattached: plain call
    attach();
    peers.slot(0).flags = kPeerActive | kPeerHost;  // ready
    peers.slot(1).flags = kPeerActive;               // active but not the 0x20 bit: skipped
    EXPECT_EQ(detour_map_item_request(nullptr, 5, 0, rec, nullptr, 0, 6), 0u);
    EXPECT_EQ(g_map_calls.size(), 1u);  // original skipped
    ASSERT_EQ(net.sent.size(), 1u);
    EXPECT_EQ(net.sent[0].target, kA);
    EXPECT_EQ(channel_of(net.sent[0]), 8);
    const auto p = payload_of(net.sent[0]);
    ASSERT_EQ(p.size(), 0x28u);
    std::uint64_t a1, a6;
    std::memcpy(&a1, p.data(), 8);
    std::memcpy(&a6, p.data() + 8, 8);
    EXPECT_EQ(a1, 5u);
    EXPECT_EQ(a6, 6u);
    EXPECT_EQ(p[16], 0x30);
    EXPECT_EQ(p[39], 0x30 + 23);
}

TEST_F(NetTest, MapItemRequestOtherModeRunsOriginalUnderLock) {
    g_map_calls.clear();
    s->original_map_item_request = reinterpret_cast<void*>(&fake_map_request);
    std::uint32_t mode = 5;  // (5 & 5) != 1
    s->mode_word = &mode;
    attach();
    s->unicast_id_a = 3;
    std::uint8_t rec[24] = {};
    EXPECT_EQ(detour_map_item_request(nullptr, 123, 0, rec, nullptr, 0, 6), 77u);
    ASSERT_EQ(g_map_calls.size(), 1u);
    EXPECT_EQ(g_map_calls[0].current, 123);  // published during the original
    EXPECT_TRUE(g_map_calls[0].locked);
    EXPECT_EQ(s->current_request.load(), -1);
    EXPECT_EQ(s->unicast_id_a.load(), -1);  // cleared afterwards
    EXPECT_TRUE(net.sent.empty());
    // mode word unset (U default): same path
    s->mode_word = nullptr;
    detour_map_item_request(nullptr, 1, 0, rec, nullptr, 0, 0);
    EXPECT_EQ(g_map_calls.size(), 2u);
}

// ---------------------------------------------------------------- P9

struct BroadcastCall {
    void* mgr;
    void* entry;
    std::uint32_t type, len;
};
std::vector<BroadcastCall> g_unicasts;
bool fake_player_send(void* mgr, void* entry, std::uint32_t type, void*, std::uint32_t len) {
    g_unicasts.push_back({mgr, entry, type, len});
    return true;
}
std::vector<BroadcastCall> g_broadcasts;
bool fake_broadcast(void* mgr, std::uint32_t type, void*, std::uint32_t len) {
    g_broadcasts.push_back({mgr, nullptr, type, len});
    return true;
}

TEST_F(NetTest, SessionBroadcastUnicastsToTheChosenMember) {
    g_unicasts.clear();
    g_broadcasts.clear();
    s->original_session_broadcast = reinterpret_cast<void*>(&fake_broadcast);
    s->player_send = &fake_player_send;
    std::uint8_t members[3 * 0xf0] = {};
    wr<std::int32_t>(members, 0xd4, 10);
    wr<std::int32_t>(members, 0xf0 + 0xd4, 20);
    wr<std::int32_t>(members, 2 * 0xf0 + 0xd4, 30);
    std::uint8_t mgr[0x90] = {};
    wr<std::uint8_t*>(mgr, 0x78, members);
    wr<std::uint8_t*>(mgr, 0x80, members + sizeof members);
    // ids unset: broadcast
    attach();
    EXPECT_TRUE(detour_session_broadcast(mgr, 0x2c, nullptr, 9));
    EXPECT_EQ(g_broadcasts.size(), 1u);
    // id a
    s->unicast_id_a = 20;
    EXPECT_TRUE(detour_session_broadcast(mgr, 0x2c, nullptr, 9));
    ASSERT_EQ(g_unicasts.size(), 1u);
    EXPECT_EQ(g_unicasts[0].entry, members + 0xf0);
    EXPECT_EQ(g_unicasts[0].type, 0x2cu);
    EXPECT_EQ(g_unicasts[0].len, 9u);
    // id a wins over id b
    s->unicast_id_b = 30;
    detour_session_broadcast(mgr, 0x2c, nullptr, 9);
    EXPECT_EQ(g_unicasts[1].entry, members + 0xf0);
    // only id b
    s->unicast_id_a = -1;
    detour_session_broadcast(mgr, 0x2c, nullptr, 9);
    EXPECT_EQ(g_unicasts[2].entry, members + 2 * 0xf0);
    // unknown id: nothing sent, false
    s->unicast_id_b = 99;
    EXPECT_FALSE(detour_session_broadcast(mgr, 0x2c, nullptr, 9));
    EXPECT_EQ(g_unicasts.size(), 3u);
    EXPECT_EQ(g_broadcasts.size(), 1u);
}

TEST_F(NetTest, SessionBroadcastFallsBackWithoutSessionOrSender) {
    g_unicasts.clear();
    g_broadcasts.clear();
    s->original_session_broadcast = reinterpret_cast<void*>(&fake_broadcast);
    s->unicast_id_a = 20;
    std::uint8_t mgr[0x90] = {};
    detour_session_broadcast(mgr, 0x2c, nullptr, 1);  // no session
    attach();
    detour_session_broadcast(mgr, 0x2c, nullptr, 1);  // session, but no player_send (U)
    EXPECT_EQ(g_broadcasts.size(), 2u);
    EXPECT_TRUE(g_unicasts.empty());
}

}  // namespace
