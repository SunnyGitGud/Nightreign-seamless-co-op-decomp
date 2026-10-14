#include "nrsc/session/frame_task.hpp"

namespace nrsc::session {

bool timer_fires(float& acc, float rate, float dt) {
    acc += dt;
    if (acc >= 1.0f / rate) {  // IEEE: 1/0 = +inf (never), 1/negative < 0 (always)
        acc = 0;
        return true;
    }
    return false;
}

void FrameTask::tick(float dt) {
    auto& slots = peers_.slots();

    // (1) handshake resend: active, not yet handshaken -> control message.
    if (timer_fires(timers_.acc1, timers_.rate1, dt))
        for (server::Peer& p : slots)
            if ((p.flags & kPeerStateMask) == server::kPeerActive) bus_.send_control(p);

    // (2) control receive, every frame, every active peer.
    for (server::Peer& e : slots) {
        if (!e.active()) continue;
        std::uint8_t b = 0;
        while (net_.pop_received(e.steam_id, kHandshakeChannel, &b, 1) != 0) {
            if ((e.flags & kPeerStateMask) == server::kPeerActive) {
                e.flags |= kPeerHandshaken;
                if (b != 0) e.flags |= kPeerRemoteBit3;
                else e.field3c = 1;
                bus_.send_control(e);  // the reply, once per peer
            }
        }
    }

    // (3) ping poll: handshaken peers.
    if (timer_fires(timers_.acc2, timers_.rate2, dt))
        for (server::Peer& p : slots)
            if ((p.flags & kPeerStateMask) == kPeerStateMask) {
                std::int32_t ping = 0;
                info_.GetSessionConnectionInfo(p.steam_id, ping);  // result ignored, ping stored regardless (C)
                pings_[p.steam_id] = ping;
            }

    // (4) counters.
    n_a_ = n_b_ = 0;
    for (const server::Peer& p : slots)
        if (p.active()) ((p.flags & kPeerRemoteBit3) ? n_b_ : n_a_)++;

    // (5) party size = bytes / 0xF0.
    members_ = member_bytes_ ? static_cast<int>(member_bytes_() / kMemberRecordSize) : 0;
}

std::int32_t FrameTask::ping_ms(SteamId id) const {
    const auto it = pings_.find(id);
    return it == pings_.end() ? -1 : it->second;
}

void FrameTask::post_job(std::function<void()> job) {
    std::lock_guard lk(job_mutex_);
    jobs_.push(std::move(job));
}

bool FrameTask::run_one_job() {
    std::function<void()> job;
    {
        std::lock_guard lk(job_mutex_);
        if (jobs_.empty()) return false;
        job = std::move(jobs_.front());
        jobs_.pop();
    }
    if (job) job();
    return true;
}

}  // namespace nrsc::session
