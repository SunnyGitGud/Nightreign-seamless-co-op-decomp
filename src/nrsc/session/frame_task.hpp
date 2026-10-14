#pragma once
// FrameTask: 0x180061140, the per-frame server_manager task, phases (1)-(5) of phase3 2.2 (C), plus the W job queue (6).
// Not thread-safe by itself (Session serialises tick()); post_job() is the one thread-safe entry.
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <mutex>
#include <queue>

#include "nrsc/hooks/net/net_group.hpp"
#include "nrsc/server/message_bus.hpp"
#include "nrsc/server/peer.hpp"
#include "nrsc/session/session_state.hpp"
#include "nrsc/session/steam_session_iface.hpp"

namespace nrsc::session {

inline constexpr std::size_t kMemberRecordSize = 0xF0;  // C: party member vector element size
inline constexpr std::uint8_t kHandshakeChannel = 1;    // C: control channel (== server::kControlChannel)

struct Timers {
    float acc1 = 0, rate1 = 1.0f;  // X+0x148 / +0x14c: handshake resend. Default rate U (1.0 picked)
    float acc2 = 0, rate2 = 1.0f;  // X+0x150 / +0x154: ping poll.        Default rate U (1.0 picked)
};

// Limiter rule (C, sweep3): acc += dt; fire (and reset acc to 0) iff acc >= 1.0f / rate. rate < 0 always fires, rate == 0 never.
bool timer_fires(float& acc, float rate, float dt);

class FrameTask {
public:
    // `peers` is both Y (C+0x88) and ctx->peers (ctx+0x28): the phase3 doc leaves their identity W and only the aliased
    // case is modelled.
    FrameTask(SessionState& state, server::PeerList& peers, server::MessageBus& bus, hooks::net::NetState& net,
              ISteamSessionInfo& info)
        : state_(state), peers_(peers), bus_(bus), net_(net), info_(info) {}

    Timers& timers() { return timers_; }

    // Phases (1)-(5). Receive (2) goes through net_.pop_received, so it only sees messages when `net_` is attached to `peers`.
    void tick(float dt);

    int active_without_bit3() const { return n_a_; }  // X+0x170
    int active_with_bit3() const { return n_b_; }     // X+0x174
    int member_count() const { return members_; }     // X+0x190
    // Last ping stored by phase (3); the original writes Peer+0x18, which server::Peer lacks (-1 = none yet).
    std::int32_t ping_ms(SteamId id) const;
    void forget_peer(SteamId id) { pings_.erase(id); }
    void clear_pings() { pings_.clear(); }

    // Size in bytes of the game's party member vector (V.end - V.begin; [[C+0x28]+0x28] in the original). U source;
    // unset = empty/null chain = 0 members.
    void set_member_bytes_source(std::function<std::size_t()> f) { member_bytes_ = std::move(f); }

    // Phase (6), W: the cross-thread job queue Z (moodycamel queue in the original, one pop per frame). post_job may be
    // called from any thread; run_one_job pops and runs at most one job (call it WITHOUT holding the session mutex).
    void post_job(std::function<void()> job);
    bool run_one_job();

private:
    [[maybe_unused]] SessionState& state_;  // kept for the phase-(7) logic that is still U
    server::PeerList& peers_;
    server::MessageBus& bus_;
    hooks::net::NetState& net_;
    ISteamSessionInfo& info_;
    Timers timers_;
    int n_a_ = 0, n_b_ = 0, members_ = 0;
    std::function<std::size_t()> member_bytes_;
    std::map<SteamId, std::int32_t> pings_;
    std::mutex job_mutex_;
    std::queue<std::function<void()>> jobs_;
};

}  // namespace nrsc::session
