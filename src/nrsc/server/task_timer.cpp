#include "nrsc/server/task_timer.hpp"

namespace nrsc::server {

void accumulate_timer(TaskTimers& timers, float dt) {
    timers.t148 += dt;
    if (timers.t148 >= kTimerThreshold && timers.t14c >= kResetGate) timers.t148 = 0.0f;
}

int mark_pending_peers(PeerList& peers, const std::function<void(Peer&)>& send_control) {
    int n = 0;
    for (Peer& p : peers.slots()) {
        if (p.flags != kPeerActive) continue;
        p.flags = kPeerMarked;
        p.field3c = 1;
        if (send_control) send_control(p);
        ++n;
    }
    return n;
}

}  // namespace nrsc::server
